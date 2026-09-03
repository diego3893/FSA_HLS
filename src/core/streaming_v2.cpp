#include "fsa/streaming_v2.hpp"

#include <hls_stream.h>

#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"

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

    struct SaTileResult{
        bool initialize = false;
        bool finalize = false;
        ap_uint<16> active_queries = 0;
        acc_t max_diff[SA_COLS]{};
        acc_t rowsum[SA_COLS]{};
        acc_t pv[SA_COLS][SA_ROWS]{};
    };

    using ElemRowStream = hls::stream<ElemRowPacket>;
    using MetaStream = hls::stream<TileMeta>;
    using SaResultStream = hls::stream<SaTileResult>;
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

    enum class PeWaveOp : std::uint8_t{
        IDLE = 0,
        QK = 1,
        SUB_MAX = 2,
        SCALE = 3,
        PWL = 4,
        ROW_SUM = 5,
        PV = 6
    };

    /**
     * 流过SA的一个控制波。partial对应Scala PE的上/下方数据，
     * index在QK时是key、PWL时是piece、PV时是feature。
     */
    struct PeWave{
        bool valid = false;
        PeWaveOp op = PeWaveOp::IDLE;
        ap_uint<4> index = 0;
        acc_t partial[SA_COLS]{};
    };

    /**
     * 一套SA_ROWS x SA_COLS物理PE依次驻留Q、S/N、P。
     *
     * 抽象层级与Scala实现一致：阵列在整个tile期间常驻。每拍只向
     * 第0行注入一个
     * 控制wave，旧wave通过行间寄存器逐拍向下传递。QK、softmax和PV
     * 在每个物理PE中只有一个peMacUnit调用点，因此不会形成按阶段复制
     * 的多套算术阵列。
     */
    void systolicArrayTile(
        const TileMeta& meta,
        const elem_t q_tile[SA_COLS][SA_ROWS],
        const elem_t k_tile[SA_COLS][SA_ROWS],
        const elem_t v_tile[SA_COLS][SA_ROWS],
        acc_t cmp_max[SA_COLS],
        SaTileResult& result
    ){
        #pragma HLS INLINE off
        // 4x4 FSA只允许16个物理MacUnit，各计算阶段按时分复用。
        #pragma HLS ALLOCATION function instances=peMacUnit limit=16
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_max type=complete dim=1

        elem_t pe_register[SA_ROWS][SA_COLS]{};
        acc_t scores[SA_COLS][SA_COLS]{};
        bool active[SA_ROWS][SA_COLS]{};
        bool pwl_done[SA_ROWS][SA_COLS]{};
        PeWave wave_pipe[SA_ROWS]{};
        #pragma HLS ARRAY_PARTITION variable=pe_register type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=scores type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=active type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pwl_done type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=wave_pipe type=complete dim=1

        for(int feature=0; feature<SA_ROWS; ++feature){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                pe_register[feature][query] = q_tile[query][feature];
            }
        }

        acc_t previous_max[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=previous_max type=complete dim=1
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            previous_max[query] = cmp_max[query];
            if(meta.initialize){
                cmp_max[query] = accMinimum();
            }
        }

        // 周期坐标与AttentionScoreExecPlan对齐：2*rows+1时S从
        // CMP回填PE，之后SUB/SCALE/PWL/ROWSUM连续流过阵列。
        static const int LOAD_S_CYCLE = 2*SA_ROWS+1;
        static const int POST_START_CYCLE = LOAD_S_CYCLE+1;
        static const int PWL_START_CYCLE = POST_START_CYCLE+2;
        static const int ROW_SUM_CYCLE = PWL_START_CYCLE+exp2PWLPieces;
        static const int PV_START_CYCLE = ROW_SUM_CYCLE+1;
        // 包含列间传播和OutputDelayer的排空窗口；4x4配置为30拍。
        static const int SA_TILE_CYCLES =
            5*SA_ROWS+exp2PWLPieces+2;

        for(int cycle=0; cycle<SA_TILE_CYCLES; ++cycle){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=SA_TILE_CYCLES max=SA_TILE_CYCLES

            if(cycle==LOAD_S_CYCLE){
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        active[row][query] = row<SA_COLS &&
                            laneEnabled(meta, query, row);
                        pwl_done[row][query] = false;
                        pe_register[row][query] = active[row][query]
                            ? cvtAtoE(scores[query][row]) : elemZero();
                    }
                }
            }

            PeWave injected{};
            #pragma HLS ARRAY_PARTITION variable=injected.partial complete dim=1
            if(cycle<SA_COLS){
                injected.valid = true;
                injected.op = PeWaveOp::QK;
                injected.index = (ap_uint<4>)cycle;
            }else if(cycle==POST_START_CYCLE){
                injected.valid = true;
                injected.op = PeWaveOp::SUB_MAX;
            }else if(cycle==POST_START_CYCLE+1){
                injected.valid = true;
                injected.op = PeWaveOp::SCALE;
            }else if(cycle>=PWL_START_CYCLE &&
                    cycle<PWL_START_CYCLE+exp2PWLPieces){
                injected.valid = true;
                injected.op = PeWaveOp::PWL;
                injected.index =
                    (ap_uint<4>)(cycle-PWL_START_CYCLE);
            }else if(cycle==ROW_SUM_CYCLE){
                injected.valid = true;
                injected.op = PeWaveOp::ROW_SUM;
            }else if(cycle>=PV_START_CYCLE &&
                    cycle<PV_START_CYCLE+SA_ROWS){
                injected.valid = true;
                injected.op = PeWaveOp::PV;
                injected.index =
                    (ap_uint<4>)(cycle-PV_START_CYCLE);
            }

            PeWave next_pipe[SA_ROWS]{};
            PeWave completed{};
            #pragma HLS ARRAY_PARTITION variable=next_pipe complete dim=1
            #pragma HLS ARRAY_PARTITION variable=completed.partial complete dim=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                PeWave wave = row==0 ? injected : wave_pipe[row-1];
                #pragma HLS ARRAY_PARTITION variable=wave.partial complete dim=1

                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    elem_t operand_a = elemZero();
                    elem_t operand_b = elemZero();
                    acc_t operand_c = accZero();
                    bool exp2_mode = false;

                    if(wave.valid){
                        switch(wave.op){
                        case PeWaveOp::QK:
                            operand_a = pe_register[row][query];
                            operand_b =
                                k_tile[wave.index.to_uint()][row];
                            operand_c = wave.partial[query];
                            break;
                        case PeWaveOp::SUB_MAX:
                            operand_a = pe_register[row][query];
                            operand_b = elemOne();
                            operand_c = -cmp_max[query];
                            break;
                        case PeWaveOp::SCALE:
                            operand_a = pe_register[row][query];
                            operand_b = elemAttentionScale();
                            break;
                        case PeWaveOp::PWL:
                            operand_a = pe_register[row][query];
                            operand_b =
                                EXP2_SLOPES[wave.index.to_uint()];
                            operand_c = exp2PWLIntercept(
                                (exp2_counter_t)wave.index
                            );
                            exp2_mode = true;
                            break;
                        case PeWaveOp::ROW_SUM:
                            operand_a = active[row][query]
                                ? pe_register[row][query] : elemZero();
                            operand_b = elemOne();
                            operand_c = wave.partial[query];
                            break;
                        case PeWaveOp::PV:
                            operand_a = active[row][query]
                                ? pe_register[row][query] : elemZero();
                            if(row<SA_COLS){
                                operand_b =
                                    v_tile[row][wave.index.to_uint()];
                            }
                            operand_c = wave.partial[query];
                            break;
                        default:
                            break;
                        }
                    }

                    // 每个物理PE在整个调度器中只有这一个算术调用点。
                    const PeMacUnitOutput unit = peMacUnit(
                        operand_a, operand_b, operand_c, exp2_mode
                    );

                    if(wave.valid && (wave.op==PeWaveOp::QK ||
                            wave.op==PeWaveOp::ROW_SUM ||
                            wave.op==PeWaveOp::PV)){
                        wave.partial[query] = unit.out_accType;
                    }else if(wave.valid && active[row][query] &&
                            (wave.op==PeWaveOp::SUB_MAX ||
                             wave.op==PeWaveOp::SCALE)){
                        pe_register[row][query] = unit.out_elemType;
                    }else if(wave.valid && active[row][query] &&
                            wave.op==PeWaveOp::PWL &&
                            !pwl_done[row][query] && unit.out_exp2){
                        pe_register[row][query] = unit.out_elemType;
                        pwl_done[row][query] = true;
                    }
                }

                next_pipe[row] = wave;
                if(row==SA_ROWS-1){
                    completed = wave;
                }
            }

            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                wave_pipe[row] = next_pipe[row];
            }

            if(completed.valid && completed.op==PeWaveOp::QK){
                const int key = completed.index.to_int();
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    scores[query][key] = completed.partial[query];
                    if(laneEnabled(meta, query, key)){
                        cmp_max[query] = accCmp(
                            completed.partial[query], cmp_max[query]
                        ).out_max;
                    }
                }
            }else if(completed.valid &&
                    completed.op==PeWaveOp::ROW_SUM){
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    result.rowsum[query] = completed.partial[query];
                }
            }else if(completed.valid && completed.op==PeWaveOp::PV){
                const int feature = completed.index.to_int();
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    result.pv[query][feature] = completed.partial[query];
                }
            }
        }

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            result.max_diff[query] =
                previous_max[query]-cmp_max[query];
        }
        result.initialize = meta.initialize;
        result.finalize = meta.finalize;
        result.active_queries = meta.active_queries;
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
        acc_t cmp_max[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_max type=complete dim=1

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

                SaTileResult result{};
                #pragma HLS ARRAY_PARTITION variable=result.max_diff type=complete dim=1
                #pragma HLS ARRAY_PARTITION variable=result.rowsum type=complete dim=1
                #pragma HLS ARRAY_PARTITION variable=result.pv type=complete dim=0
                systolicArrayTile(
                    meta, q_tile, k_tile, v_tile, cmp_max, result
                );
                sa_result_stream.write(result);
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
                const SaTileResult result = sa_result_stream.read();
                acc_t alpha[SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=alpha type=complete dim=1

                // 与FSA一致：oldMax-newMax的exp2属于Accumulator，而非SA。
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    alpha[query] = result.initialize
                        ? accZero()
                        : accExp2PWL(
                            result.max_diff[query]*attentionScale()
                        );
                }

                if(result.initialize){
                    for(int row=0; row<ACC_ROWS; ++row){
                        #pragma HLS PIPELINE II=1
                        for(int query=0; query<SA_COLS; ++query){
                            #pragma HLS UNROLL
                            accumulator_sram[row][query] = accZero();
                        }
                    }
                }

                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    accumulator_sram[0][query] = accUnit(
                        alpha[query],
                        accumulator_sram[0][query],
                        result.rowsum[query]
                    );
                }
                for(int feature=0; feature<SA_ROWS; ++feature){
                    #pragma HLS PIPELINE II=1
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        accumulator_sram[feature+1][query] = accUnit(
                            alpha[query],
                            accumulator_sram[feature+1][query],
                            result.pv[query][feature]
                        );
                    }
                }

                if(result.finalize){
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
                            query<result.active_queries.to_int(); ++query){
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
