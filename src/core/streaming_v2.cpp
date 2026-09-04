#include "fsa/streaming_v2.hpp"

#include <hls_stream.h>
#include <utils/x_hls_utils.h>

#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"
#include "fsa/state.hpp"

namespace fsa{
namespace streaming_v2_detail{

    static_assert(
        SA_ROWS>=SA_COLS,
        "streaming v2要求SA高度不小于token tile宽度"
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
    using MetaStream = hls::stream<TileMeta>;
    using SaResultStream = hls::stream<SaResultToken>;
    using AccRowStream = hls::stream<AccRowPacket>;
    using DmaWordStream = hls::stream<dma_word_t>;

    unsigned tileCount(const unsigned length){
        #pragma HLS INLINE
        return (length+(unsigned)SA_COLS-1U)/(unsigned)SA_COLS;
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

    /**
     * DMA流先写入显式双缓冲Scratchpad，再由整行端口送入SA流。
     * Q只从DDR读取一次/Query tile，但会从Q SRAM为每个KV tile重播。
     */
    void scratchpadProcess(
        const unsigned length,
        const bool causal,
        ElemRowStream& q_dma_stream,
        ElemRowStream& k_dma_stream,
        ElemRowStream& v_dma_stream,
        MetaStream& meta_stream,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream
    ){
        #pragma HLS INLINE off

        elem_t q_sram[2][SA_COLS][SA_ROWS]{};
        elem_t k_sram[2][SA_COLS][SA_ROWS]{};
        elem_t v_sram[2][SA_COLS][SA_ROWS]{};
        bool q_valid[2][SA_COLS]{};
        bool k_valid[2][SA_COLS]{};
        #pragma HLS BIND_STORAGE variable=q_sram type=ram_t2p impl=bram
        #pragma HLS BIND_STORAGE variable=k_sram type=ram_t2p impl=bram
        #pragma HLS BIND_STORAGE variable=v_sram type=ram_t2p impl=bram
        #pragma HLS ARRAY_RESHAPE variable=q_sram type=complete dim=3
        #pragma HLS ARRAY_RESHAPE variable=k_sram type=complete dim=3
        #pragma HLS ARRAY_RESHAPE variable=v_sram type=complete dim=3
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
                    q_sram[q_bank][query_lane][feature] =
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
                        k_sram[kv_bank][key_lane][feature] =
                            k_packet.data[feature];
                        v_sram[kv_bank][key_lane][feature] =
                            v_packet.data[feature];
                    }
                }

                TileMeta meta{};
                meta.initialize = key_tile==0;
                meta.finalize = key_tile+1U==tiles;
                meta.causal = causal;
                meta.query_base = query_tile*(unsigned)SA_COLS;
                meta.key_base = key_tile*(unsigned)SA_COLS;
                const unsigned remaining_queries =
                    length-meta.query_base.to_uint();
                const unsigned remaining_keys =
                    length-meta.key_base.to_uint();
                meta.active_queries = (ap_uint<16>)(
                    remaining_queries<(unsigned)SA_COLS
                        ? remaining_queries : (unsigned)SA_COLS
                );
                meta.active_keys = (ap_uint<16>)(
                    remaining_keys<(unsigned)SA_COLS
                        ? remaining_keys : (unsigned)SA_COLS
                );
                meta_stream.write(meta);

                // FSA中P会覆盖PE reg，因此每个KV tile都从Q SRAM重载Q。
                for(int query_lane=0;
                        query_lane<SA_COLS; ++query_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket packet{};
                    packet.valid = q_valid[q_bank][query_lane];
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        packet.data[feature] =
                            q_sram[q_bank][query_lane][feature];
                    }
                    q_sa_stream.write(packet);
                }

                for(int key_lane=0; key_lane<SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket k_packet{};
                    ElemRowPacket v_packet{};
                    k_packet.valid = k_valid[kv_bank][key_lane];
                    v_packet.valid = k_valid[kv_bank][key_lane];
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        k_packet.data[feature] =
                            k_sram[kv_bank][key_lane][feature];
                        v_packet.data[feature] =
                            v_sram[kv_bank][key_lane][feature];
                    }
                    k_sa_stream.write(k_packet);
                    v_sa_stream.write(v_packet);
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
     */
    struct PeWave{
        bool valid = false;
        PeWaveOp op = PeWaveOp::IDLE;
        PeWaveDirection direction = PeWaveDirection::DOWN;
        PeWaveIndex index = 0;
        acc_t partial[SA_COLS]{};
        elem_t element[SA_COLS]{};
        bool exp2_match[SA_COLS]{};
    };

    /** score从顶部CMP逐拍向下回流；key标签决定在哪一行写入PE.reg。 */
    struct ScoreWave{
        bool valid = false;
        PeWaveIndex key = 0;
        elem_t score[SA_COLS]{};
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

    /**
     * @brief 每列顶部唯一的CMP实例
     *
     * UPDATE的最大值选择使用有限FP32位序比较，使连续score能够II=1更新
     * newMax；差值仍由accCmp中的单条FMA通路完成。这样既保留FSA的
     * “每列一个CMP”结构，也避免多拍浮点比较形成列内反馈环。
     */
    template<int COL>
    acc_t spatialCmpCell(
        const bool valid,
        const CmpWaveOp op,
        const bool input_enabled,
        const acc_t d_input,
        CMPState& state
    ){
        static_assert(COL>=0 && COL<SA_COLS, "CMP col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1

        if(!valid || op==CmpWaveOp::HOLD){
            return accZero();
        }

        if(op==CmpWaveOp::RESET){
            state.oldMax = accMinimum();
            state.newMax = accMinimum();
            return accZero();
        }

        if(op==CmpWaveOp::UPDATE){
            const acc_t masked_input = input_enabled
                ? d_input : accMinimum();
            state.newMax = finiteAccMax(masked_input, state.newMax);
            // 与Scala CMP一致：score从CMP向下返回前先缩为elem_t。
            return viewEasA(cvtAtoE(masked_input));
        }

        if(op==CmpWaveOp::PROP_EXP2_INTERCEPTS){
            const acc_t output = exp2PWLIntercept(state.exp2_counter);
            state.exp2_counter = state.exp2_counter+1;
            return output;
        }

        if(op==CmpWaveOp::PROP_ZERO){
            return accZero();
        }

        const acc_t lhs = op==CmpWaveOp::PROP_MAX
            ? accZero() : state.oldMax;
        const CmpUnitOutput cmp_output = accCmp(lhs, state.newMax);
        if(op==CmpWaveOp::PROP_MAX_DIFF){
            state.oldMax = state.newMax;
        }
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
            d_output[COL] = spatialCmpCell<COL>(
                valid, op, enabled, d_input[COL], state[COL]
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
            const elem_t operand_a = use_probability && !active[ROW][COL]
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
        SaResultStream& result_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_state type=complete dim=1

        constexpr int KEY_TILE = SA_COLS;
        // 与当前peMacUnit综合延迟一致；data/valid/op/tag共用此延迟。
        constexpr int PE_TOKEN_LATENCY = 9;
        constexpr int QK_START = SA_COLS;
        constexpr int FIRST_SCORE =
            QK_START+SA_ROWS*PE_TOKEN_LATENCY;
        constexpr int SCORES_READY = FIRST_SCORE+2*KEY_TILE-2;
        constexpr int MAX_DIFF_CYCLE = SCORES_READY+1;
        constexpr int SUB_MAX_CYCLE = MAX_DIFF_CYCLE+1;
        constexpr int SCALE_CYCLE =
            SUB_MAX_CYCLE+PE_TOKEN_LATENCY+1;
        constexpr int PWL_START = SCALE_CYCLE+PE_TOKEN_LATENCY+1;
        constexpr int PWL_END = PWL_START+exp2PWLPieces-1;
        constexpr int ROW_SUM_CYCLE =
            PWL_END+PE_TOKEN_LATENCY+1;
        constexpr int PV_START = ROW_SUM_CYCLE+1;
        constexpr int LAST_RESULT_CYCLE =
            PV_START+SA_ROWS-1+SA_ROWS*PE_TOKEN_LATENCY;
        constexpr int TOTAL_CYCLES = LAST_RESULT_CYCLE+1;

        elem_t pe_register[SA_ROWS][SA_COLS]{};
        bool active[SA_ROWS][SA_COLS]{};
        PeWave pe_pipeline[SA_ROWS][PE_TOKEN_LATENCY]{};
        ScoreWave score_pipeline[SA_ROWS]{};
        #pragma HLS ARRAY_PARTITION variable=pe_register complete dim=0
        #pragma HLS ARRAY_PARTITION variable=active complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pe_pipeline complete dim=0
        #pragma HLS ARRAY_PARTITION variable=score_pipeline complete dim=1

        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                active[row][query] = row<KEY_TILE &&
                    laneEnabled(meta, query, row);
            }
        }

        for(int cycle=0; cycle<TOTAL_CYCLES; ++cycle){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_FLATTEN off
            // 调度器保证下一次读取发生在对应commit后；同拍RAW仍保留。
            #pragma HLS DEPENDENCE variable=pe_register inter false

            const int pipeline_slot = cycle%PE_TOKEN_LATENCY;
            PeWave row_input[SA_ROWS]{};
            PeWave row_result[SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=row_input complete dim=0
            #pragma HLS ARRAY_PARTITION variable=row_result complete dim=0

            PeWave qk_at_cmp{};
            PeWave bottom_result{};

            // 每个PE行取出九拍前启动的token，提交本行结果后送往邻行。
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                const PeWave completed = pe_pipeline[row][pipeline_slot];
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

            // Tile feeder每拍装入一个query列。SRAM读延迟与InputDelayer
            // 属于本轮明确不修改的存储边界。
            if(cycle<SA_COLS){
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    pe_register[row][cycle] = q_tile[cycle][row];
                }
            }

            // 连续KEY_TILE拍从阵列底部启动QK wave。
            if(cycle>=QK_START && cycle<QK_START+KEY_TILE){
                PeWave source{};
                #pragma HLS ARRAY_PARTITION variable=source.partial complete dim=1
                source.valid = true;
                source.op = PeWaveOp::QK;
                source.direction = PeWaveDirection::UP;
                source.index = (PeWaveIndex)(cycle-QK_START);
                row_input[SA_ROWS-1] = source;
            }

            CmpWaveOp cmp_op = CmpWaveOp::HOLD;
            bool cmp_valid = false;
            int cmp_item = 0;
            if(cycle==0 && meta.initialize){
                cmp_valid = true;
                cmp_op = CmpWaveOp::RESET;
            }else if(qk_at_cmp.valid){
                cmp_valid = true;
                cmp_op = CmpWaveOp::UPDATE;
                cmp_item = qk_at_cmp.index.to_int();
            }else if(cycle==MAX_DIFF_CYCLE){
                cmp_valid = true;
                cmp_op = CmpWaveOp::PROP_MAX_DIFF;
            }else if(cycle==SUB_MAX_CYCLE){
                cmp_valid = true;
                cmp_op = CmpWaveOp::PROP_MAX;
            }else if(cycle>=PWL_START && cycle<=PWL_END){
                cmp_valid = true;
                cmp_op = CmpWaveOp::PROP_EXP2_INTERCEPTS;
                cmp_item = cycle-PWL_START;
            }else if(cycle==ROW_SUM_CYCLE){
                cmp_valid = true;
                cmp_op = CmpWaveOp::PROP_ZERO;
            }

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
                const ScoreWave score_wave = row==0 && injected_score.valid
                    ? injected_score : score_pipeline[row];
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
            int source_item = 0;
            if(cycle==SUB_MAX_CYCLE){
                down_source.valid = true;
                down_source.op = PeWaveOp::SUB_MAX;
            }else if(cycle==SCALE_CYCLE){
                down_source.valid = true;
                down_source.op = PeWaveOp::SCALE;
            }else if(cycle>=PWL_START && cycle<=PWL_END){
                source_item = cycle-PWL_START;
                down_source.valid = true;
                down_source.op = PeWaveOp::PWL;
                down_source.index = (PeWaveIndex)source_item;
            }else if(cycle==ROW_SUM_CYCLE){
                down_source.valid = true;
                down_source.op = PeWaveOp::ROW_SUM;
            }else if(cycle>=PV_START && cycle<PV_START+SA_ROWS){
                source_item = cycle-PV_START;
                down_source.valid = true;
                down_source.op = PeWaveOp::PV;
                down_source.index = (PeWaveIndex)source_item;
            }
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
                    horizontal[row] = row<KEY_TILE
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
        MetaStream& meta_stream,
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
                const TileMeta meta = meta_stream.read();

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
                    cmp_state, sa_result_stream
                );
            }
        }
    }

    /**
     * FSA Accumulator：row0保存L，row1..SA_ROWS保存O；列bank完全分割。
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

                // rowsum后紧跟SA_ROWS个PV token。每拍只更新一行L/O，
                // 同一组SA_COLS个Accumulator lane在所有行之间时分复用。
                for(int event=0; event<SA_ROWS+1; ++event){
                    #pragma HLS PIPELINE II=1
                    const SaResultToken value_token =
                        sa_result_stream.read();
                    const int accumulator_row =
                        value_token.kind==SaResultKind::ROW_SUM
                            ? 0 : value_token.index.to_int()+1;
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        const acc_t old_value = max_token.initialize
                            ? accZero()
                            : accumulator_sram[accumulator_row][query];
                        accumulator_sram[accumulator_row][query] = accUnit(
                            alpha[query],
                            old_value,
                            value_token.data[query]
                        );
                    }
                }

                if(max_token.finalize){
                    acc_t inverse_l[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=inverse_l type=complete dim=1
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        inverse_l[query] = accumulator_sram[0][query]!=accZero()
                            ? accumulator_reciprocal(
                                accumulator_sram[0][query]
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
                                accumulator_sram[feature+1][query]*
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
     * 再妨碍Vitis把DMA、Scratchpad、单一SA和Accumulator抽取成并行
     * 进程。QK、softmax和PV在同一个SA进程中顺序复用唯一PE网格。
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
        MetaStream meta_stream("v2_meta");
        ElemRowStream q_sa_stream("v2_q_sa");
        ElemRowStream k_sa_stream("v2_k_sa");
        ElemRowStream v_sa_stream("v2_v_sa");
        SaResultStream sa_result_stream("v2_sa_result");
        AccRowStream output_stream("v2_output");
        DmaWordStream output_word_stream("v2_output_words");
        #pragma HLS STREAM variable=q_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=meta_stream depth=2
        #pragma HLS STREAM variable=q_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=sa_result_stream depth=2
        #pragma HLS STREAM variable=output_stream depth=2
        #pragma HLS STREAM variable=output_word_stream \
            depth=2*SA_COLS*DMA_O_WORDS_PER_ROW
        #pragma HLS DATAFLOW

        dmaReadQ(q_address, length, q_dma_stream);
        dmaReadK(k_address, length, k_dma_stream);
        dmaReadV(v_address, length, v_dma_stream);
        scratchpadProcess(
            length, causal,
            q_dma_stream, k_dma_stream, v_dma_stream,
            meta_stream, q_sa_stream, k_sa_stream, v_sa_stream
        );
        systolicArrayProcess(
            length,
            meta_stream, q_sa_stream, k_sa_stream, v_sa_stream,
            sa_result_stream
        );
        accumulatorProcess(length, sa_result_stream, output_stream);
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
