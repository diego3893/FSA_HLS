#include "fsa/streaming_v2.hpp"

#include <hls_stream.h>
#include <utils/x_hls_utils.h>

#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"
#include "fsa/delayer.hpp"
#include "fsa/state.hpp"

namespace fsa{
namespace streaming_v2_detail{

    static_assert(
        SA_ROWS>=SA_COLS,
        "streaming v2要求SA高度不小于token tile宽度"
    );

    constexpr int SPAD_Q_BASE_ADDRESS = 0;
    constexpr int SPAD_K_BASE_ADDRESS =
        SPAD_Q_BASE_ADDRESS+SA_COLS;
    constexpr int SPAD_VT_BASE_ADDRESS =
        SPAD_K_BASE_ADDRESS+SA_ROWS;

    static_assert(
        SPAD_VT_BASE_ADDRESS+SA_ROWS<=SPAD_ROWS,
        "streaming v2的Q/K/V_t布局超出Scratchpad容量"
    );

    // 与FSA execution plan一致：8拍依次从SA左侧广播FP16 PWL斜率。
    const elem_t EXP2_SLOPES[exp2PWLPieces] = {
        (elem_t)0.664062500F,
        (elem_t)0.608886719F,
        (elem_t)0.558105469F,
        (elem_t)0.512207031F,
        (elem_t)0.469482422F,
        (elem_t)0.430419922F,
        (elem_t)0.394775391F,
        (elem_t)0.362060547F
    };

    struct ElemRowPacket{
        bool valid = false;
        elem_t data[SA_ROWS]{};
    };

    struct AccRowPacket{
        acc_t data[SA_ROWS]{};
    };

    struct TileMeta{
        bool initialize = false;
        bool finalize = false;
        bool causal = false;
        ap_uint<16> active_queries = 0;
        ap_uint<16> active_keys = 0;
        ap_uint<32> query_base = 0;
        ap_uint<32> key_base = 0;
    };

    /**
     * @brief 一条FSA指令在Scratchpad出口选择的InputDelayer布局。
     *
     * 三个字段直接对应MatrixInstruction.spad中的revInput、
     * delayOutput和revOutput。控制器只生成布局，数据通路只消费布局，
     * 避免把FSA的五阶段控制重新塞回SA算术函数。
     */
    struct InputLayoutControl{
        bool rev_input = false;
        bool delay_output = false;
        bool rev_output = false;
    };

    /**
     * @brief 一个query/KV tile对应的FSA Core控制token。
     *
     * LOAD_STATIONARY、ATTENTION_SCORE和ATTENTION_VALUE三条指令的
     * Scratchpad/Delayer控制随tile一起流过Core。LSE更新和最终归一化
     * 由同一tile产生的SA结果token继续驱动Accumulator阶段。
     */
    struct CoreTileControl{
        TileMeta meta{};
        InputLayoutControl load_stationary{};
        InputLayoutControl attention_score{};
        InputLayoutControl attention_value{};
    };

    enum class SaResultKind : std::uint8_t{
        MAX_DIFF = 0,
        ROW_SUM = 1,
        PV = 2
    };

    /**
     * SA与Accumulator之间的逐拍结果token。
     * MAX_DIFF先到达，随后是ROW_SUM和连续SA_ROWS拍PV；不再等待并
     * 物化完整SaTileResult后才启动Accumulator。
     */
    struct SaResultToken{
        SaResultKind kind = SaResultKind::MAX_DIFF;
        bool initialize = false;
        bool finalize = false;
        ap_uint<16> active_queries = 0;
        ap_uint<16> index = 0;
        acc_t data[SA_COLS]{};
    };

    using ElemRowStream = hls::stream<ElemRowPacket>;
    using CoreControlStream = hls::stream<CoreTileControl>;
    using SaResultStream = hls::stream<SaResultToken>;
    using AccRowStream = hls::stream<AccRowPacket>;
    using DmaWordStream = hls::stream<dma_word_t>;

    unsigned tileCount(const unsigned length){
        #pragma HLS INLINE
        return (length+(unsigned)SA_COLS-1U)/(unsigned)SA_COLS;
    }

    /**
     * @brief 逐tile生成与旧ExecutionPlan一致的Core阶段控制。
     *
     * 该进程只产生控制token，不调用状态数据通路。因而控制生成可以与
     * DMA、Scratchpad和前一个tile的计算通过DATAFLOW并行推进。
     */
    void fsaCoreControllerProcess(
        const unsigned length,
        const bool causal,
        CoreControlStream& control_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                #pragma HLS PIPELINE II=1
                CoreTileControl control{};
                control.meta.initialize = key_tile==0;
                control.meta.finalize = key_tile+1U==tiles;
                control.meta.causal = causal;
                control.meta.query_base =
                    query_tile*(unsigned)SA_COLS;
                control.meta.key_base = key_tile*(unsigned)SA_COLS;

                const unsigned remaining_queries =
                    length-control.meta.query_base.to_uint();
                const unsigned remaining_keys =
                    length-control.meta.key_base.to_uint();
                control.meta.active_queries = (ap_uint<16>)(
                    remaining_queries<(unsigned)SA_COLS
                        ? remaining_queries : (unsigned)SA_COLS
                );
                control.meta.active_keys = (ap_uint<16>)(
                    remaining_keys<(unsigned)SA_COLS
                        ? remaining_keys : (unsigned)SA_COLS
                );

                // LOAD_STATIONARY：Q按Scratchpad读出的自然顺序直通。
                control.load_stationary = InputLayoutControl{};

                // ATTENTION_SCORE：与旧requestInstruction完全相同。
                control.attention_score.rev_input = true;
                control.attention_score.delay_output = true;
                control.attention_score.rev_output = true;

                // ATTENTION_VALUE：V_t使用相同输入反转和阶梯延迟，
                // 但不执行最终输出反转。
                control.attention_value.rev_input = true;
                control.attention_value.delay_output = true;
                control.attention_value.rev_output = false;

                control_stream.write(control);
            }
        }
    }

    void dmaReadQ(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        const unsigned length,
        ElemRowStream& q_dma_stream
    ){
        #pragma HLS INLINE off

        const unsigned query_tiles = tileCount(length);
        for(unsigned query_tile=0;
                query_tile<query_tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(int lane=0; lane<SA_COLS; ++lane){
                #pragma HLS PIPELINE II=1
                ElemRowPacket packet{};
                const unsigned query =
                    query_tile*(unsigned)SA_COLS+(unsigned)lane;
                packet.valid = query<length;
                if(packet.valid){
                    dma_load_elem_row(q_address, query, packet.data);
                }
                q_dma_stream.write(packet);
            }
        }
    }

    void dmaReadK(
        const dma_word_t k_address[DMA_MAX_QKV_WORDS],
        const unsigned length,
        ElemRowStream& k_dma_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket packet{};
                    const unsigned key =
                        key_tile*(unsigned)SA_COLS+(unsigned)lane;
                    packet.valid = key<length;
                    if(packet.valid){
                        dma_load_elem_row(k_address, key, packet.data);
                    }
                    k_dma_stream.write(packet);
                }
            }
        }
    }

    void dmaReadV(
        const dma_word_t v_address[DMA_MAX_QKV_WORDS],
        const unsigned length,
        ElemRowStream& v_dma_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket packet{};
                    const unsigned key =
                        key_tile*(unsigned)SA_COLS+(unsigned)lane;
                    packet.valid = key<length;
                    if(packet.valid){
                        dma_load_elem_row(v_address, key, packet.data);
                    }
                    v_dma_stream.write(packet);
                }
            }
        }
    }

    /** @brief FSA Scratchpad唯一整行同步读端口。 */
    void scratchpadReadRow(
        const elem_t spad_sram[2][SPAD_ROWS][SA_ROWS],
        const unsigned bank,
        const int address,
        elem_t data[SA_ROWS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=1 max=1
        for(int feature=0; feature<SA_ROWS; ++feature){
            #pragma HLS UNROLL
            data[feature] = spad_sram[bank][address][feature];
        }
    }

    /**
     * DMA流先写入显式双缓冲Scratchpad，再通过唯一一拍整行读端口送入
     * InputDelayer。Q只从DDR读取一次/Query tile，但会为每个KV tile重播。
     */
    void scratchpadProcess(
        const unsigned length,
        ElemRowStream& q_dma_stream,
        ElemRowStream& k_dma_stream,
        ElemRowStream& v_dma_stream,
        CoreControlStream& control_in,
        CoreControlStream& control_out,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION \
            function instances=scratchpadReadRow limit=1

        // 与旧FSA Core相同，Q/K/V_t共享一个逻辑Scratchpad地址空间。
        // 最外层两个物理bank用于tile级ping-pong；最后一维是一整行，
        // 对应SA_ROWS个并行elem_t。
        elem_t spad_sram[2][SPAD_ROWS][SA_ROWS]{};
        bool q_valid[2][SA_COLS]{};
        bool k_valid[2][SA_COLS]{};
        #pragma HLS BIND_STORAGE variable=spad_sram type=ram_t2p impl=bram
        #pragma HLS ARRAY_PARTITION variable=spad_sram type=complete dim=1
        #pragma HLS ARRAY_RESHAPE variable=spad_sram type=complete dim=3
        #pragma HLS ARRAY_PARTITION variable=q_valid type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=k_valid type=complete dim=2

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned q_bank = query_tile&1U;

            for(int query_lane=0; query_lane<SA_COLS; ++query_lane){
                #pragma HLS PIPELINE II=1
                const ElemRowPacket packet = q_dma_stream.read();
                q_valid[q_bank][query_lane] = packet.valid;
                for(int feature=0; feature<SA_ROWS; ++feature){
                    #pragma HLS UNROLL
                    spad_sram[q_bank]
                        [SPAD_Q_BASE_ADDRESS+query_lane][feature] =
                        packet.data[feature];
                }
            }

            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const unsigned kv_bank = key_tile&1U;

                for(int key_lane=0; key_lane<SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    const ElemRowPacket k_packet = k_dma_stream.read();
                    const ElemRowPacket v_packet = v_dma_stream.read();
                    k_valid[kv_bank][key_lane] = k_packet.valid;
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        spad_sram[kv_bank]
                            [SPAD_K_BASE_ADDRESS+key_lane][feature] =
                            k_packet.data[feature];
                        // ATTENTION_VALUE按feature行读取V_t。
                        spad_sram[kv_bank]
                            [SPAD_VT_BASE_ADDRESS+feature][key_lane] =
                            v_packet.data[feature];
                    }
                }

                // 控制token与本tile的Scratchpad数据一起向下游推进。
                const CoreTileControl control = control_in.read();
                control_out.write(control);

                // FSA中P会覆盖PE reg，因此每个KV tile都从Q SRAM重载Q。
                for(int query_lane=0;
                        query_lane<SA_COLS; ++query_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket packet{};
                    packet.valid = q_valid[q_bank][query_lane];
                    elem_t row_data[SA_ROWS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=row_data complete dim=1
                    scratchpadReadRow(
                        spad_sram,
                        q_bank,
                        SPAD_Q_BASE_ADDRESS+query_lane,
                        row_data
                    );
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        packet.data[feature] = row_data[feature];
                    }
                    q_sa_stream.write(packet);
                }

                for(int key_lane=0; key_lane<SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket k_packet{};
                    k_packet.valid = k_valid[kv_bank][key_lane];
                    elem_t row_data[SA_ROWS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=row_data complete dim=1
                    scratchpadReadRow(
                        spad_sram,
                        kv_bank,
                        SPAD_K_BASE_ADDRESS+key_lane,
                        row_data
                    );
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        k_packet.data[feature] = row_data[feature];
                    }
                    k_sa_stream.write(k_packet);
                }

                // ATTENTION_VALUE按feature顺序整行读取V_t，再为当前
                // 多周期SA适配器恢复成每个key一个packet。
                elem_t v_transposed[SA_ROWS][SA_ROWS]{};
                #pragma HLS ARRAY_PARTITION \
                    variable=v_transposed complete dim=2
                for(int feature=0; feature<SA_ROWS; ++feature){
                    #pragma HLS PIPELINE II=1
                    scratchpadReadRow(
                        spad_sram,
                        kv_bank,
                        SPAD_VT_BASE_ADDRESS+feature,
                        v_transposed[feature]
                    );
                }
                for(int key_lane=0; key_lane<SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket v_packet{};
                    v_packet.valid = k_valid[kv_bank][key_lane];
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        v_packet.data[feature] =
                            v_transposed[feature][key_lane];
                    }
                    v_sa_stream.write(v_packet);
                }
            }
        }
    }

    /**
     * @brief 显式FSA InputDelayer阶段。
     *
     * Scratchpad以完整tile提供Q/K/V行。这里按旧ExecutionPlan的布局控制
     * 逐拍驱动唯一一套InputDelayer，并把错拍输出重新收集为下游SA使用的
     * tile。重新收集只适配当前多周期PE调度器；实际数据必须经过Delayer
     * 状态寄存器，不能再由SA直接索引原始K/V绕过该模块。
     */
    void inputDelayerProcess(
        const unsigned length,
        CoreControlStream& control_in,
        ElemRowStream& q_spad_stream,
        ElemRowStream& k_spad_stream,
        ElemRowStream& v_spad_stream,
        CoreControlStream& control_out,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream
    ){
        #pragma HLS INLINE off

        ElemInputDelayerState delayer_state{};
        #pragma HLS ARRAY_PARTITION \
            variable=delayer_state.out_delay_pipe type=complete dim=0

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const CoreTileControl control = control_in.read();

                ElemRowPacket source[3][SA_COLS]{};
                ElemRowPacket restored[3][SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=source complete dim=0
                #pragma HLS ARRAY_PARTITION variable=restored complete dim=0

                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    source[0][lane] = q_spad_stream.read();
                    source[1][lane] = k_spad_stream.read();
                    source[2][lane] = v_spad_stream.read();
                    restored[0][lane].valid = source[0][lane].valid;
                    restored[1][lane].valid = source[1][lane].valid;
                    restored[2][lane].valid = source[2][lane].valid;
                }

                for(int phase=0; phase<3; ++phase){
                    // LOAD_STATIONARY、SCORE和VALUE时分复用同一套Delayer。
                    reset_input_delayer_state(delayer_state);
                    const InputLayoutControl layout = phase==0
                        ? control.load_stationary
                        : (phase==1
                            ? control.attention_score
                            : control.attention_value);

                    for(int cycle=0;
                            cycle<SA_COLS+SA_ROWS-1; ++cycle){
                        #pragma HLS PIPELINE II=1
                        InputDelayerIO io{};
                        io.in.valid = cycle<SA_COLS;
                        io.in.bits.rev_input = layout.rev_input;
                        io.in.bits.delay_output = layout.delay_output;
                        io.in.bits.rev_output = layout.rev_output;
                        if(cycle<SA_COLS){
                            for(int feature=0;
                                    feature<SA_ROWS; ++feature){
                                #pragma HLS UNROLL
                                io.in.bits.data[(std::size_t)feature] =
                                    source[phase][cycle].data[feature];
                            }
                        }

                        ElemInputDelayerState next_state{};
                        #pragma HLS ARRAY_PARTITION \
                            variable=next_state.out_delay_pipe \
                            type=complete dim=0
                        input_delayer_step(
                            delayer_state, next_state, io
                        );
                        delayer_state = next_state;

                        // 由rev/delay配置反推出当前输出来自哪个tile行和
                        // feature，将真实错拍波前恢复成多周期SA的tile输入。
                        for(int output_lane=0;
                                output_lane<SA_ROWS; ++output_lane){
                            #pragma HLS UNROLL
                            const int internal_lane = layout.rev_output
                                ? SA_ROWS-1-output_lane : output_lane;
                            const int source_feature = layout.rev_input
                                ? SA_ROWS-1-internal_lane : internal_lane;
                            const int source_lane = cycle-
                                (layout.delay_output ? internal_lane : 0);
                            if(source_lane>=0 && source_lane<SA_COLS){
                                restored[phase][source_lane]
                                    .data[source_feature] =
                                    io.out[(std::size_t)output_lane];
                            }
                        }
                    }
                }

                control_out.write(control);
                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    q_sa_stream.write(restored[0][lane]);
                    k_sa_stream.write(restored[1][lane]);
                    v_sa_stream.write(restored[2][lane]);
                }
            }
        }
    }

    bool laneEnabled(
        const TileMeta& meta,
        const int query,
        const int key
    ){
        #pragma HLS INLINE
        if(query>=meta.active_queries.to_int() ||
                key>=meta.active_keys.to_int()){
            return false;
        }
        return !meta.causal ||
            meta.key_base.to_uint()+(unsigned)key<=
            meta.query_base.to_uint()+(unsigned)query;
    }

    /**
     * @brief 有限FP32的组合max选择
     *
     * Attention score不会产生NaN。直接比较IEEE位序可以保留
     * Scala CMP中的组合max mux，避免HLS生成一个多拍浮点
     * compare并再次形成key间反馈。CMP的浮点差值通路仍由
     * result.max_diff的四个列通路保留。
     */
    acc_t finiteAccMax(const acc_t a, const acc_t b){
        #pragma HLS INLINE
        const fp_struct<acc_t> a_view(a);
        const fp_struct<acc_t> b_view(b);
        const ap_uint<32> a_bits = a_view.data();
        const ap_uint<32> b_bits = b_view.data();
        const bool a_sign = a_bits[31];
        const bool b_sign = b_bits[31];

        if(a_sign != b_sign){
            return a_sign ? b : a;
        }
        if(a_sign){
            return a_bits<b_bits ? a : b;
        }
        return a_bits>b_bits ? a : b;
    }

    enum class PeWaveOp : std::uint8_t{
        IDLE = 0,
        QK = 1,
        SUB_MAX = 2,
        SCALE = 3,
        PWL = 4,
        ROW_SUM = 5,
        PV = 6
    };

    enum class PeWaveDirection : std::uint8_t{
        UP = 0,
        DOWN = 1
    };

    constexpr int maxConstexpr(const int a, const int b){
        return a>b ? a : b;
    }

    constexpr int unsignedWidth(const unsigned value){
        return value<=1U ? 1 : 1+unsignedWidth(value>>1);
    }

    constexpr int PE_WAVE_ITEM_COUNT = maxConstexpr(
        maxConstexpr(SA_ROWS, SA_COLS), exp2PWLPieces
    );
    constexpr int PE_WAVE_INDEX_WIDTH = unsignedWidth(
        (unsigned)(PE_WAVE_ITEM_COUNT-1)
    );
    using PeWaveIndex = ap_uint<PE_WAVE_INDEX_WIDTH>;

    /**
     * 流过SA的一个控制波。partial对应Scala PE的上/下方数据，
     * index在QK时是key、PWL时是piece、PV时是feature。
     *
     * 成员刻意不设默认值：临时wave用`{}`显式生成bubble，环形存储则
     * 依靠写前不读协议，避免HLS在每个tile入口综合出整表清零循环。
     */
    struct PeWave{
        bool valid;
        PeWaveOp op;
        PeWaveDirection direction;
        PeWaveIndex index;
        acc_t partial[SA_COLS];
        elem_t element[SA_COLS];
        bool exp2_match[SA_COLS];
    };

    /** score从顶部CMP逐拍向下回流；key标签决定在哪一行写入PE.reg。 */
    struct ScoreWave{
        bool valid;
        PeWaveIndex key;
        elem_t score[SA_COLS];
    };

    /**
     * @brief streaming v2中CMP在一个wave上的动作
     *
     * HOLD表示本拍CMP不推进状态。其余命令与Scala CMP.scala中的
     * UPDATE/PROP_*一一对应；RESET只在一个query tile的首个KV tile执行。
     */
    enum class CmpWaveOp : std::uint8_t{
        HOLD = 0,
        UPDATE = 1,
        PROP_MAX = 2,
        PROP_MAX_DIFF = 3,
        PROP_ZERO = 4,
        RESET = 5,
        PROP_EXP2_INTERCEPTS = 6
    };

    // 单套物理SA的逐拍微程序参数。PE算术流水本身为9拍；额外7拍是
    // 当前HLS调度器从环形槽读取到结果写回的固定前后级。所有阶段边界
    // 集中在这里生成，数据通路不再自行推导“当前是哪一拍”。
    constexpr int PE_TOKEN_LATENCY = 9;
    constexpr int PE_SCHEDULER_GUARD_CYCLES = 7;
    constexpr int PE_HOP_CYCLES =
        PE_TOKEN_LATENCY+PE_SCHEDULER_GUARD_CYCLES;
    constexpr int QK_START = SA_COLS;
    constexpr int FIRST_SCORE =
        QK_START+SA_ROWS*PE_HOP_CYCLES;
    constexpr int SCORES_READY = FIRST_SCORE+2*SA_COLS-2;
    constexpr int MAX_DIFF_CYCLE = SCORES_READY+1;
    constexpr int SUB_MAX_CYCLE = MAX_DIFF_CYCLE+1;
    constexpr int SCALE_CYCLE =
        SUB_MAX_CYCLE+PE_HOP_CYCLES+1;
    constexpr int PWL_START = SCALE_CYCLE+PE_HOP_CYCLES+1;
    constexpr int PWL_END = PWL_START+exp2PWLPieces-1;
    constexpr int ROW_SUM_CYCLE =
        PWL_END+PE_HOP_CYCLES+1;
    constexpr int PV_START = ROW_SUM_CYCLE+1;
    constexpr int LAST_RESULT_CYCLE =
        PV_START+SA_ROWS-1+SA_ROWS*PE_HOP_CYCLES;
    constexpr int SA_TILE_CYCLES = LAST_RESULT_CYCLE+1;

    /**
     * @brief ExecutionPlan送入单套SA的一拍控制。
     *
     * 这相当于Scala ExecutionPlanStep中与PE/CMP有关的部分。控制token
     * 不携带数据，QK/softmax/PV在同一套PE阵列中按时间复用。
     */
    struct SaCycleControl{
        bool load_query = false;
        PeWaveIndex query_index = 0;
        bool launch_qk = false;
        PeWaveIndex qk_index = 0;
        bool cmp_valid = false;
        CmpWaveOp cmp_op = CmpWaveOp::HOLD;
        PeWaveIndex cmp_item = 0;
        bool launch_down = false;
        PeWaveOp down_op = PeWaveOp::IDLE;
        PeWaveIndex down_item = 0;
    };

    using SaCycleControlStream = hls::stream<SaCycleControl>;

    SaCycleControl makeSaCycleControl(
        const int cycle,
        const bool initialize
    ){
        #pragma HLS INLINE

        SaCycleControl control{};
        if(cycle<SA_COLS){
            control.load_query = true;
            control.query_index = (PeWaveIndex)cycle;
        }
        if(cycle>=QK_START && cycle<QK_START+SA_COLS){
            control.launch_qk = true;
            control.qk_index = (PeWaveIndex)(cycle-QK_START);
        }

        if(cycle==0 && initialize){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::RESET;
        }else if(cycle>=FIRST_SCORE &&
                cycle<FIRST_SCORE+SA_COLS){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::UPDATE;
            control.cmp_item =
                (PeWaveIndex)(cycle-FIRST_SCORE);
        }else if(cycle==MAX_DIFF_CYCLE){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_MAX_DIFF;
        }else if(cycle==SUB_MAX_CYCLE){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_MAX;
        }else if(cycle>=PWL_START && cycle<=PWL_END){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_EXP2_INTERCEPTS;
            control.cmp_item = (PeWaveIndex)(cycle-PWL_START);
        }else if(cycle==ROW_SUM_CYCLE){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_ZERO;
        }

        if(cycle==SUB_MAX_CYCLE){
            control.launch_down = true;
            control.down_op = PeWaveOp::SUB_MAX;
        }else if(cycle==SCALE_CYCLE){
            control.launch_down = true;
            control.down_op = PeWaveOp::SCALE;
        }else if(cycle>=PWL_START && cycle<=PWL_END){
            control.launch_down = true;
            control.down_op = PeWaveOp::PWL;
            control.down_item = (PeWaveIndex)(cycle-PWL_START);
        }else if(cycle==ROW_SUM_CYCLE){
            control.launch_down = true;
            control.down_op = PeWaveOp::ROW_SUM;
        }else if(cycle>=PV_START && cycle<PV_START+SA_ROWS){
            control.launch_down = true;
            control.down_op = PeWaveOp::PV;
            control.down_item = (PeWaveIndex)(cycle-PV_START);
        }
        return control;
    }

    /**
     * @brief 逐拍产生SA微程序，不阻塞tile/地址控制的预取路径。
     *
     * 拆成独立DATAFLOW actor后，Scratchpad可以继续提前准备后续tile；
     * 此处仅在SA消费速度不足时通过本控制FIFO自然反压。
     */
    void saExecutionPlanProcess(
        const unsigned length,
        SaCycleControlStream& cycle_control_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                for(int cycle=0; cycle<SA_TILE_CYCLES; ++cycle){
                    #pragma HLS PIPELINE II=1
                    cycle_control_stream.write(
                        makeSaCycleControl(cycle, key_tile==0)
                    );
                }
            }
        }
    }

    /**
     * @brief 每列CMP的多周期输出通路
     *
     * oldMax/newMax/exp2_counter均按值传入，函数内部不修改CMP状态。
     * 这样FP32差值和FP32到FP16转换可以保持II=1流水，而不会把函数
     * latency错误地放到newMax的逐拍反馈环上。状态寄存器仍然只有
     * SA_COLS组，并在SpatialCmpColumns中与该输出通路一一对应。
     */
    template<int COL>
    acc_t spatialCmpOutputCell(
        const bool valid,
        const CmpWaveOp op,
        const bool input_enabled,
        const acc_t d_input,
        const acc_t old_max,
        const acc_t new_max,
        const exp2_counter_t exp2_counter
    ){
        static_assert(COL>=0 && COL<SA_COLS, "CMP col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1

        if(!valid || op==CmpWaveOp::HOLD){
            return accZero();
        }

        if(op==CmpWaveOp::RESET){
            return accZero();
        }

        if(op==CmpWaveOp::UPDATE){
            const acc_t masked_input = input_enabled
                ? d_input : accMinimum();
            // 与Scala CMP一致：score从CMP向下返回前先缩为elem_t。
            return viewEasA(cvtAtoE(masked_input));
        }

        if(op==CmpWaveOp::PROP_EXP2_INTERCEPTS){
            return exp2PWLIntercept(exp2_counter);
        }

        if(op==CmpWaveOp::PROP_ZERO){
            return accZero();
        }

        const acc_t lhs = op==CmpWaveOp::PROP_MAX
            ? accZero() : old_max;
        const CmpUnitOutput cmp_output = accCmp(lhs, new_max);
        return cmp_output.out_diff;
    }

    template<int COL>
    struct SpatialCmpColumns{
        static void run(
            const bool valid,
            const CmpWaveOp op,
            const int key,
            const TileMeta& meta,
            const acc_t d_input[SA_COLS],
            CMPState state[SA_COLS],
            acc_t d_output[SA_COLS]
        ){
            #pragma HLS INLINE
            const bool enabled = op!=CmpWaveOp::UPDATE ||
                laneEnabled(meta, COL, key);
            const acc_t old_max = state[COL].oldMax;
            const acc_t new_max = state[COL].newMax;
            const exp2_counter_t exp2_counter = state[COL].exp2_counter;

            // 只有这个组合max位于连续score之间的真实反馈环。它不经过
            // cvtAtoE或accCmp，因而下一拍可以立即读取更新后的newMax。
            if(valid && op==CmpWaveOp::UPDATE){
                const acc_t masked_input = enabled
                    ? d_input[COL] : accMinimum();
                state[COL].newMax = finiteAccMax(
                    masked_input, new_max
                );
            }else if(valid && op==CmpWaveOp::RESET){
                state[COL].oldMax = accMinimum();
                state[COL].newMax = accMinimum();
            }else if(valid && op==CmpWaveOp::PROP_MAX_DIFF){
                state[COL].oldMax = new_max;
            }else if(valid && op==CmpWaveOp::PROP_EXP2_INTERCEPTS){
                state[COL].exp2_counter = exp2_counter+1;
            }

            d_output[COL] = spatialCmpOutputCell<COL>(
                valid, op, enabled, d_input[COL],
                old_max, new_max, exp2_counter
            );
            SpatialCmpColumns<COL+1>::run(
                valid, op, key, meta, d_input, state, d_output
            );
        }
    };

    template<>
    struct SpatialCmpColumns<SA_COLS>{
        static void run(
            const bool,
            const CmpWaveOp,
            const int,
            const TileMeta&,
            const acc_t[SA_COLS],
            CMPState[SA_COLS],
            acc_t[SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    /**
     * 每个坐标特化为一个不内联的PE层次。这不是为不同坐标
     * 实现不同算法，而是告诉HLS这些调用是同时存在的空间PE，
     * 不得把它们折叠为少量共享运算器。每个PE内部仍只有一个
     * 原始peMacUnit，MAC和exp2按控制时分复用同一条通路。
     */
    template<int ROW, int COL>
    PeMacUnitOutput spatialPeCell(
        const elem_t operand_a,
        const elem_t operand_b,
        const acc_t operand_c,
        const bool exp2_mode
    ){
        static_assert(ROW>=0 && ROW<SA_ROWS, "PE row out of range");
        static_assert(COL>=0 && COL<SA_COLS, "PE col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=9 max=9
        return peMacUnit(
            operand_a, operand_b, operand_c, exp2_mode
        );
    }

    template<int ROW, int COL>
    struct SpatialPeColumns{
        static void run(
            PeWave& wave,
            const elem_t horizontal[SA_ROWS],
            elem_t pe_register[SA_ROWS][SA_COLS],
            const bool active[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
            const bool reduce = wave.op==PeWaveOp::QK ||
                wave.op==PeWaveOp::ROW_SUM ||
                wave.op==PeWaveOp::PV;
            const bool use_probability = wave.op==PeWaveOp::ROW_SUM ||
                wave.op==PeWaveOp::PV;
            // A tile no longer clears every PE register before the cycle loop.
            // Invalid bubbles must therefore not observe a register before the
            // feeder has written it.  Every valid operation is launched only
            // after the corresponding Q/score/probability value is resident.
            const elem_t operand_a = !wave.valid ||
                    (use_probability && !active[ROW][COL])
                ? elemZero() : pe_register[ROW][COL];
            const bool exp2_mode = wave.op==PeWaveOp::PWL;

            const PeMacUnitOutput unit = spatialPeCell<ROW, COL>(
                operand_a,
                horizontal[ROW],
                wave.partial[COL],
                exp2_mode
            );

            if(wave.valid && reduce){
                wave.partial[COL] = unit.out_accType;
            }else if(wave.valid && active[ROW][COL] &&
                    (wave.op==PeWaveOp::SUB_MAX ||
                     wave.op==PeWaveOp::SCALE)){
                wave.element[COL] = unit.out_elemType;
            }else if(wave.valid && active[ROW][COL] &&
                    wave.op==PeWaveOp::PWL){
                wave.element[COL] = unit.out_elemType;
                wave.exp2_match[COL] = unit.out_exp2;
            }

            SpatialPeColumns<ROW, COL+1>::run(
                wave,
                horizontal,
                pe_register,
                active
            );
        }
    };

    template<int ROW>
    struct SpatialPeColumns<ROW, SA_COLS>{
        static void run(
            PeWave&,
            const elem_t[SA_ROWS],
            elem_t[SA_ROWS][SA_COLS],
            const bool[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    template<int ROW>
    struct SpatialPeRowsTick{
        static void run(
            PeWave wave[SA_ROWS],
            const elem_t horizontal[SA_ROWS],
            elem_t pe_register[SA_ROWS][SA_COLS],
            const bool active[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
            SpatialPeColumns<ROW, 0>::run(
                wave[ROW],
                horizontal,
                pe_register,
                active
            );
            SpatialPeRowsTick<ROW+1>::run(
                wave,
                horizontal,
                pe_register,
                active
            );
        }
    };

    template<>
    struct SpatialPeRowsTick<SA_ROWS>{
        static void run(
            PeWave[SA_ROWS],
            const elem_t[SA_ROWS],
            elem_t[SA_ROWS][SA_COLS],
            const bool[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    /**
     * @brief 一套常驻的CMP + PE阵列完成一个KV tile
     *
     * 本函数内部只有一个CMP调用点和一个PE调用点。所有QK、softmax、
     * row-sum和PV命令进入同一个II=1调度循环，因而不会再把每个wave
     * 解释成一次ap_ctrl_hs子模块事务。结构固定为SA_COLS个列头CMP以及
     * SA_ROWS x SA_COLS个PE，规模随配置参数变化。
     */

    /**
     * @brief 一套常驻CMP + PE阵列的逐拍token引擎
     *
     * QK token从底行向上，score经顶部CMP后每拍向下一行restream；
     * softmax、row-sum和PV token从顶部向下。每个(row,col)只有一个
     * spatialPeCell调用点，多个token可同时驻留在各PE的FMA流水级中。
     * S/N/P始终只保存在pe_register，不再物化外部score/probability阵列。
     */
    void spatialSystolicArrayTileTick(
        const TileMeta& meta,
        const elem_t q_tile[SA_COLS][SA_ROWS],
        const elem_t k_tile[SA_COLS][SA_ROWS],
        const elem_t v_tile[SA_COLS][SA_ROWS],
        CMPState cmp_state[SA_COLS],
        SaCycleControlStream& cycle_control_stream,
        SaResultStream& result_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_state type=complete dim=1

        // These arrays are completely written before their first meaningful
        // read.  Avoid aggregate initialization here: in HLS it becomes a
        // serialized per-tile clear loop and hides the II=1 wavefront gain.
        elem_t pe_register[SA_ROWS][SA_COLS];
        bool active[SA_ROWS][SA_COLS];
        PeWave pe_pipeline[SA_ROWS][PE_HOP_CYCLES];
        ScoreWave score_pipeline[SA_ROWS];
        #pragma HLS ARRAY_PARTITION variable=pe_register complete dim=0
        #pragma HLS ARRAY_PARTITION variable=active complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pe_pipeline complete dim=0
        #pragma HLS ARRAY_PARTITION variable=score_pipeline complete dim=1

        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                active[row][query] = row<SA_COLS &&
                    laneEnabled(meta, query, row);
            }
        }

        for(int cycle=0; cycle<SA_TILE_CYCLES; ++cycle){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_FLATTEN off
            // 调度器保证下一次读取发生在对应commit后；同拍RAW仍保留。
            #pragma HLS DEPENDENCE variable=pe_register inter false

            const SaCycleControl cycle_control =
                cycle_control_stream.read();

            const int pipeline_slot = cycle%PE_HOP_CYCLES;
            PeWave row_input[SA_ROWS]{};
            PeWave row_result[SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=row_input complete dim=0
            #pragma HLS ARRAY_PARTITION variable=row_result complete dim=0

            PeWave qk_at_cmp{};
            PeWave bottom_result{};

            // 环形槽在前PE_HOP_CYCLES拍被逐槽写满；之后每次读取的槽都已
            // 由同一tile写过，因此不需要在tile入口清空整个环形缓冲。
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                if(cycle>=PE_HOP_CYCLES){
                    const PeWave completed =
                        pe_pipeline[row][pipeline_slot];
                    if(completed.valid){
                        for(int query=0; query<SA_COLS; ++query){
                            #pragma HLS UNROLL
                            if(active[row][query] &&
                                    (completed.op==PeWaveOp::SUB_MAX ||
                                     completed.op==PeWaveOp::SCALE)){
                                pe_register[row][query] =
                                    completed.element[query];
                            }else if(active[row][query] &&
                                    completed.op==PeWaveOp::PWL &&
                                    completed.exp2_match[query]){
                                pe_register[row][query] =
                                    completed.element[query];
                            }
                        }

                        if(completed.direction==PeWaveDirection::UP){
                            if(row==0){
                                qk_at_cmp = completed;
                            }else{
                                row_input[row-1] = completed;
                            }
                        }else if(row+1==SA_ROWS){
                            bottom_result = completed;
                        }else{
                            row_input[row+1] = completed;
                        }
                    }
                }
            }

            // ExecutionPlan每拍允许装入一个query列；数据已经依次经过
            // Scratchpad的一拍整行读边界和InputDelayer。
            if(cycle_control.load_query){
                const int query_index =
                    cycle_control.query_index.to_int();
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    pe_register[row][query_index] =
                        q_tile[query_index][row];
                }
            }

            // 连续SA_COLS拍从阵列底部启动QK wave。
            if(cycle_control.launch_qk){
                PeWave source{};
                #pragma HLS ARRAY_PARTITION variable=source.partial complete dim=1
                source.valid = true;
                source.op = PeWaveOp::QK;
                source.direction = PeWaveDirection::UP;
                source.index = cycle_control.qk_index;
                row_input[SA_ROWS-1] = source;
            }

            CmpWaveOp cmp_op = cycle_control.cmp_op;
            bool cmp_valid = cycle_control.cmp_valid;
            int cmp_item = cycle_control.cmp_item.to_int();

            acc_t cmp_input[SA_COLS]{};
            acc_t cmp_output[SA_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=cmp_input complete dim=1
            #pragma HLS ARRAY_PARTITION variable=cmp_output complete dim=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                cmp_input[query] = qk_at_cmp.partial[query];
            }
            SpatialCmpColumns<0>::run(
                cmp_valid, cmp_op, cmp_item, meta,
                cmp_input, cmp_state, cmp_output
            );

            ScoreWave injected_score{};
            #pragma HLS ARRAY_PARTITION variable=injected_score.score complete dim=1
            if(cmp_op==CmpWaveOp::UPDATE){
                injected_score.valid = true;
                injected_score.key = (PeWaveIndex)cmp_item;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    injected_score.score[query] =
                        viewAasE(cmp_output[query]);
                }
            }

            // Score从CMP每拍向下一行移动；到tag对应行时原地覆盖Q。
            ScoreWave next_score_pipeline[SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=next_score_pipeline complete dim=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                ScoreWave score_wave{};
                if(row==0 && injected_score.valid){
                    score_wave = injected_score;
                }else if(cycle>0){
                    // cycle 0 writes every score slot before any later read.
                    score_wave = score_pipeline[row];
                }
                if(score_wave.valid){
                    if(score_wave.key.to_int()==row){
                        for(int query=0; query<SA_COLS; ++query){
                            #pragma HLS UNROLL
                            pe_register[row][query] = active[row][query]
                                ? score_wave.score[query] : elemZero();
                        }
                    }
                    if(row+1<SA_ROWS){
                        next_score_pipeline[row+1] = score_wave;
                    }
                }
            }
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                score_pipeline[row] = next_score_pipeline[row];
            }

            PeWave down_source{};
            #pragma HLS ARRAY_PARTITION variable=down_source.partial complete dim=1
            down_source.valid = cycle_control.launch_down;
            down_source.op = cycle_control.down_op;
            down_source.index = cycle_control.down_item;
            down_source.direction = PeWaveDirection::DOWN;
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                if(down_source.op==PeWaveOp::SUB_MAX ||
                        down_source.op==PeWaveOp::PWL ||
                        down_source.op==PeWaveOp::ROW_SUM){
                    down_source.partial[query] = cmp_output[query];
                }
            }
            if(down_source.valid){
                row_input[0] = down_source;
            }

            elem_t horizontal[SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=horizontal complete dim=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                const PeWaveOp op = row_input[row].op;
                const int item = row_input[row].index.to_int();
                if(op==PeWaveOp::QK){
                    horizontal[row] = k_tile[item][row];
                }else if(op==PeWaveOp::SCALE){
                    horizontal[row] = elemAttentionScale();
                }else if(op==PeWaveOp::PWL){
                    horizontal[row] = EXP2_SLOPES[item];
                }else if(op==PeWaveOp::PV){
                    horizontal[row] = row<SA_COLS
                        ? v_tile[row][item] : elemZero();
                }else{
                    horizontal[row] = elemOne();
                }
                row_result[row] = row_input[row];
            }

            SpatialPeRowsTick<0>::run(
                row_result, horizontal, pe_register, active
            );
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                pe_pipeline[row][pipeline_slot] = row_result[row];
            }

            if(cmp_op==CmpWaveOp::PROP_MAX_DIFF){
                SaResultToken token{};
                #pragma HLS ARRAY_PARTITION variable=token.data complete dim=1
                token.kind = SaResultKind::MAX_DIFF;
                token.initialize = meta.initialize;
                token.finalize = meta.finalize;
                token.active_queries = meta.active_queries;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    token.data[query] = cmp_output[query];
                }
                result_stream.write(token);
            }else if(bottom_result.valid &&
                    bottom_result.op==PeWaveOp::ROW_SUM){
                SaResultToken token{};
                #pragma HLS ARRAY_PARTITION variable=token.data complete dim=1
                token.kind = SaResultKind::ROW_SUM;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    token.data[query] = bottom_result.partial[query];
                }
                result_stream.write(token);
            }else if(bottom_result.valid &&
                    bottom_result.op==PeWaveOp::PV){
                SaResultToken token{};
                #pragma HLS ARRAY_PARTITION variable=token.data complete dim=1
                token.kind = SaResultKind::PV;
                token.index = bottom_result.index;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    token.data[query] = bottom_result.partial[query];
                }
                result_stream.write(token);
            }
        }
    }

    void systolicArrayProcess(
        const unsigned length,
        CoreControlStream& control_stream,
        SaCycleControlStream& cycle_control_stream,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream,
        SaResultStream& sa_result_stream
    ){
        #pragma HLS INLINE off

        elem_t q_tile[SA_COLS][SA_ROWS]{};
        elem_t k_tile[SA_COLS][SA_ROWS]{};
        elem_t v_tile[SA_COLS][SA_ROWS]{};
        CMPState cmp_state[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_state type=complete dim=1

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const CoreTileControl control = control_stream.read();
                const TileMeta meta = control.meta;

                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS PIPELINE II=1
                    const ElemRowPacket packet = q_sa_stream.read();
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        q_tile[query][feature] = packet.data[feature];
                    }
                }
                for(int key=0; key<SA_COLS; ++key){
                    #pragma HLS PIPELINE II=1
                    const ElemRowPacket k_packet = k_sa_stream.read();
                    const ElemRowPacket v_packet = v_sa_stream.read();
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        k_tile[key][feature] = k_packet.data[feature];
                        v_tile[key][feature] = v_packet.data[feature];
                    }
                }

                spatialSystolicArrayTileTick(
                    meta, q_tile, k_tile, v_tile,
                    cmp_state, cycle_control_stream, sa_result_stream
                );
            }
        }
    }

    /**
     * @brief 显式FSA OutputDelayer阶段。
     *
     * 当前多周期SA在一个SaResultToken中给出完整列向量。这里先按物理SA
     * 底边的列错拍顺序逐列注入，再由唯一一套OutputDelayer恢复为同拍的
     * SA_COLS路Accumulator输入。这样Accumulator不能再绕过输出对齐网络。
     */
    void outputDelayerProcess(
        const unsigned length,
        SaResultStream& raw_result_stream,
        SaResultStream& aligned_result_stream
    ){
        #pragma HLS INLINE off

        OutputDelayerState delayer_state{};
        #pragma HLS ARRAY_PARTITION \
            variable=delayer_state.out_delay_pipe type=complete dim=0

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                for(int token_index=0;
                        token_index<SA_ROWS+2; ++token_index){
                    const SaResultToken raw = raw_result_stream.read();
                    SaResultToken aligned = raw;
                    reset_output_delayer_state(delayer_state);

                    for(int cycle=0; cycle<SA_COLS; ++cycle){
                        #pragma HLS PIPELINE II=1
                        OutputDelayerIO io{};
                        io.in[(std::size_t)cycle] = raw.data[cycle];

                        OutputDelayerState next_state{};
                        #pragma HLS ARRAY_PARTITION \
                            variable=next_state.out_delay_pipe \
                            type=complete dim=0
                        output_delayer_step(
                            delayer_state, next_state, io
                        );
                        delayer_state = next_state;

                        if(cycle+1==SA_COLS){
                            for(int col=0; col<SA_COLS; ++col){
                                #pragma HLS UNROLL
                                aligned.data[col] =
                                    io.out[(std::size_t)col];
                            }
                        }
                    }
                    aligned_result_stream.write(aligned);
                }
            }
        }
    }

    /**
     * @brief 一列Accumulator的物理FP32 MAC流水线
     *
     * 模板列号保证综合后得到SA_COLS条并行lane。每条lane在不同L/O行
     * 之间时分复用，QK和PV仍只使用上方同一套SA。固定九拍只增加
     * fill/drain，不降低连续L/O token的发射率。
     */
    template<int COL>
    acc_t accumulatorMacLane(
        const acc_t scale,
        const acc_t old_value,
        const acc_t contribution
    ){
        static_assert(COL>=0 && COL<SA_COLS,
                      "Accumulator col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=9 max=9
        return accUnit(scale, old_value, contribution);
    }

    template<int COL>
    struct AccumulatorMacColumns{
        static void run(
            const acc_t scale[SA_COLS],
            const acc_t old_value[SA_COLS],
            const acc_t contribution[SA_COLS],
            acc_t result[SA_COLS]
        ){
            #pragma HLS INLINE
            result[COL] = accumulatorMacLane<COL>(
                scale[COL], old_value[COL], contribution[COL]
            );
            AccumulatorMacColumns<COL+1>::run(
                scale, old_value, contribution, result
            );
        }
    };

    template<>
    struct AccumulatorMacColumns<SA_COLS>{
        static void run(
            const acc_t[SA_COLS],
            const acc_t[SA_COLS],
            const acc_t[SA_COLS],
            acc_t[SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    /** @brief AccRAM整行同步读边界。 */
    void accumulatorSramReadRow(
        const acc_t accumulator_sram[ACC_ROWS][SA_COLS],
        const int address,
        acc_t data[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=1 max=1
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            data[col] = accumulator_sram[address][col];
        }
    }

    /** @brief AccRAM整行同步写边界。 */
    void accumulatorSramWriteRow(
        acc_t accumulator_sram[ACC_ROWS][SA_COLS],
        const int address,
        const acc_t data[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=1 max=1
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            accumulator_sram[address][col] = data[col];
        }
    }

    /**
     * @brief FSA Accumulator算术与显式AccRAM端口。
     *
     * RAM仍由本DATAFLOW进程独占，避免形成C仿真和RTL都可能死锁的
     * 双向进程环；所有访问必须经过两个非内联的一拍行端口，算术逻辑
     * 不再直接索引L/O数组。
     */
    void accumulatorProcess(
        const unsigned length,
        SaResultStream& sa_result_stream,
        AccRowStream& output_stream
    ){
        #pragma HLS INLINE off

        acc_t accumulator_sram[ACC_ROWS][SA_COLS]{};
        #pragma HLS BIND_STORAGE \
            variable=accumulator_sram type=ram_t2p impl=bram
        #pragma HLS ARRAY_PARTITION \
            variable=accumulator_sram type=complete dim=2

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const SaResultToken max_token = sa_result_stream.read();
                acc_t alpha[SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=alpha type=complete dim=1

                // 与FSA一致：oldMax-newMax的exp2属于Accumulator，而非SA。
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    alpha[query] = max_token.initialize
                        ? accZero()
                        : accExp2PWL(
                            max_token.data[query]*attentionScale()
                        );
                }

                // rowsum后紧跟SA_ROWS个PV token。协议固定映射为
                // event=0更新L，event=1..SA_ROWS更新对应O行。
                for(int event=0; event<SA_ROWS+1; ++event){
                    #pragma HLS PIPELINE II=1
                    const SaResultToken value_token =
                        sa_result_stream.read();
                    acc_t old_value[SA_COLS]{};
                    acc_t contribution[SA_COLS]{};
                    acc_t updated_value[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=old_value complete dim=1
                    #pragma HLS ARRAY_PARTITION \
                        variable=contribution complete dim=1
                    #pragma HLS ARRAY_PARTITION \
                        variable=updated_value complete dim=1
                    accumulatorSramReadRow(
                        accumulator_sram, event, old_value
                    );
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        old_value[query] = max_token.initialize
                            ? accZero()
                            : old_value[query];
                        contribution[query] = value_token.data[query];
                    }
                    AccumulatorMacColumns<0>::run(
                        alpha, old_value, contribution, updated_value
                    );
                    accumulatorSramWriteRow(
                        accumulator_sram, event, updated_value
                    );
                }

                if(max_token.finalize){
                    acc_t final_rows[ACC_ROWS][SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=final_rows complete dim=2
                    for(int row=0; row<ACC_ROWS; ++row){
                        #pragma HLS PIPELINE II=1
                        accumulatorSramReadRow(
                            accumulator_sram, row, final_rows[row]
                        );
                    }

                    acc_t inverse_l[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=inverse_l type=complete dim=1
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        inverse_l[query] = final_rows[0][query]!=accZero()
                            ? accumulator_reciprocal(
                                final_rows[0][query]
                            )
                            : accZero();
                    }

                    for(int query=0;
                            query<max_token.active_queries.to_int(); ++query){
                        #pragma HLS PIPELINE II=1
                        AccRowPacket packet{};
                        for(int feature=0; feature<SA_ROWS; ++feature){
                            #pragma HLS UNROLL
                            packet.data[feature] =
                                final_rows[feature+1][query]*
                                inverse_l[query];
                        }
                        output_stream.write(packet);
                    }
                }
            }
        }
    }

    /**
     * Accumulator输出与AXI写事务解耦。每个FP32行连续打包成64-bit word，
     * 使后级只处理单一宽度的顺序数据流。
     */
    void outputPackProcess(
        const unsigned length,
        AccRowStream& output_stream,
        DmaWordStream& output_word_stream
    ){
        #pragma HLS INLINE off

        for(unsigned query=0; query<length; ++query){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_SEQUENCE_LENGTH
            const AccRowPacket packet = output_stream.read();
            for(int word=0; word<DMA_O_WORDS_PER_ROW; ++word){
                #pragma HLS PIPELINE II=1
                acc_t values[DMA_ACCS_PER_WORD]{};
                #pragma HLS ARRAY_PARTITION variable=values type=complete dim=1
                for(int lane=0; lane<DMA_ACCS_PER_WORD; ++lane){
                    #pragma HLS UNROLL
                    values[lane] = packet.data[
                        word*DMA_ACCS_PER_WORD+lane
                    ];
                }
                output_word_stream.write(dma_pack_acc_word(values));
            }
        }
    }

    /**
     * 单一扁平循环产生完整O矩阵的连续地址写，便于m_axi合并成长burst。
     */
    void dmaWriteO(
        dma_word_t o_address[DMA_MAX_O_WORDS],
        const unsigned length,
        DmaWordStream& output_word_stream,
        ap_uint<8>& status
    ){
        #pragma HLS INLINE off

        const unsigned total_words =
            length*(unsigned)DMA_O_WORDS_PER_ROW;
        for(unsigned word=0; word<total_words; ++word){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT \
                min=DMA_O_WORDS_PER_ROW max=DMA_MAX_O_WORDS
            o_address[word] = output_word_stream.read();
        }
        status = (ap_uint<8>)static_cast<std::uint8_t>(
            FsaStreamingV2Status::OK
        );
    }

    /**
     * 规范DATAFLOW区域只包含局部stream声明和进程调用。外层参数检查不
     * 再妨碍Vitis把DMA、Core控制器、Scratchpad、InputDelayer、单一SA、
     * OutputDelayer、Accumulator和AccRAM抽取成并行进程。QK、softmax
     * 和PV仍在同一个SA进程中顺序复用唯一PE网格。
     */
    void fsaStreamingDataflow(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        const dma_word_t k_address[DMA_MAX_QKV_WORDS],
        const dma_word_t v_address[DMA_MAX_QKV_WORDS],
        dma_word_t o_address[DMA_MAX_O_WORDS],
        const unsigned length,
        const bool causal,
        ap_uint<8>& status
    ){
        #pragma HLS INLINE off

        ElemRowStream q_dma_stream("v2_q_dma");
        ElemRowStream k_dma_stream("v2_k_dma");
        ElemRowStream v_dma_stream("v2_v_dma");
        CoreControlStream control_to_spad("v2_control_to_spad");
        CoreControlStream control_to_delayer("v2_control_to_delayer");
        CoreControlStream control_to_sa("v2_control_to_sa");
        SaCycleControlStream sa_cycle_control_stream(
            "v2_sa_cycle_control"
        );
        ElemRowStream q_spad_stream("v2_q_spad");
        ElemRowStream k_spad_stream("v2_k_spad");
        ElemRowStream v_spad_stream("v2_v_spad");
        ElemRowStream q_sa_stream("v2_q_sa");
        ElemRowStream k_sa_stream("v2_k_sa");
        ElemRowStream v_sa_stream("v2_v_sa");
        SaResultStream raw_sa_result_stream("v2_raw_sa_result");
        SaResultStream aligned_sa_result_stream("v2_aligned_sa_result");
        AccRowStream output_stream("v2_output");
        DmaWordStream output_word_stream("v2_output_words");
        #pragma HLS STREAM variable=q_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=control_to_spad depth=2
        #pragma HLS STREAM variable=control_to_delayer depth=2
        #pragma HLS STREAM variable=control_to_sa depth=2
        #pragma HLS STREAM variable=sa_cycle_control_stream depth=32
        #pragma HLS STREAM variable=q_spad_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_spad_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_spad_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=q_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=raw_sa_result_stream depth=2
        #pragma HLS STREAM variable=aligned_sa_result_stream depth=2
        #pragma HLS STREAM variable=output_stream depth=2
        #pragma HLS STREAM variable=output_word_stream \
            depth=2*SA_COLS*DMA_O_WORDS_PER_ROW
        #pragma HLS DATAFLOW

        fsaCoreControllerProcess(length, causal, control_to_spad);
        saExecutionPlanProcess(length, sa_cycle_control_stream);
        dmaReadQ(q_address, length, q_dma_stream);
        dmaReadK(k_address, length, k_dma_stream);
        dmaReadV(v_address, length, v_dma_stream);
        scratchpadProcess(
            length,
            q_dma_stream, k_dma_stream, v_dma_stream,
            control_to_spad, control_to_delayer,
            q_spad_stream, k_spad_stream, v_spad_stream
        );
        inputDelayerProcess(
            length,
            control_to_delayer,
            q_spad_stream, k_spad_stream, v_spad_stream,
            control_to_sa,
            q_sa_stream, k_sa_stream, v_sa_stream
        );
        systolicArrayProcess(
            length,
            control_to_sa, sa_cycle_control_stream,
            q_sa_stream, k_sa_stream, v_sa_stream,
            raw_sa_result_stream
        );
        outputDelayerProcess(
            length, raw_sa_result_stream, aligned_sa_result_stream
        );
        accumulatorProcess(
            length, aligned_sa_result_stream, output_stream
        );
        outputPackProcess(length, output_stream, output_word_stream);
        dmaWriteO(o_address, length, output_word_stream, status);
    }

}  // namespace streaming_v2_detail

void fsa_streaming_v2_run(
    const dma_word_t q_address[DMA_MAX_QKV_WORDS],
    const dma_word_t k_address[DMA_MAX_QKV_WORDS],
    const dma_word_t v_address[DMA_MAX_QKV_WORDS],
    dma_word_t o_address[DMA_MAX_O_WORDS],
    const ap_uint<32> sequence_length,
    const bool causal,
    ap_uint<8>& status
){
    status = (ap_uint<8>)static_cast<std::uint8_t>(
        FsaStreamingV2Status::INVALID_SEQUENCE_LENGTH
    );
    const unsigned length = sequence_length.to_uint();
    if(length==0 || length>(unsigned)MAX_SEQUENCE_LENGTH){
        return;
    }

    streaming_v2_detail::fsaStreamingDataflow(
        q_address, k_address, v_address, o_address,
        length, causal, status
    );
}

}  // namespace fsa
