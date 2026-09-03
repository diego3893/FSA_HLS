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

    enum class PeArrayMode : std::uint8_t{
        REDUCE = 0,
        UPDATE_MAC = 1,
        UPDATE_PWL = 2
    };

    /**
     * 一次wave通过完整PE网格。resident只读，计算结果经独立端口返回；
     * 调用方在wave完成后决定是否写回。这样QK/PV等只读阶段不会再被HLS
     * 误判为对resident的循环携带依赖，同时所有阶段仍复用同一个PE网格。
     */
    void peArrayWave(
        const elem_t resident[SA_ROWS][SA_COLS],
        const elem_t horizontal[SA_ROWS],
        const acc_t vertical[SA_ROWS][SA_COLS],
        const bool active[SA_ROWS][SA_COLS],
        const PeArrayMode mode,
        const acc_t encoded_intercept,
        elem_t updated[SA_ROWS][SA_COLS],
        bool update_enable[SA_ROWS][SA_COLS],
        acc_t output[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS ARRAY_PARTITION variable=resident type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=horizontal type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=vertical type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=active type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=updated type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=update_enable type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=output type=complete dim=1

        acc_t partial[SA_ROWS+1][SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=partial type=complete dim=0
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            partial[0][query] = accZero();
        }
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                const bool reduce = mode==PeArrayMode::REDUCE;
                const bool update_pwl = mode==PeArrayMode::UPDATE_PWL;
                const bool execute = reduce || active[row][query];
                PeMacUnitOutput unit{};
                if(execute){
                    unit = peMacUnit(
                        resident[row][query],
                        horizontal[row],
                        reduce ? partial[row][query]
                               : (update_pwl ? encoded_intercept
                                             : vertical[row][query]),
                        update_pwl
                    );
                }

                partial[row+1][query] = reduce
                    ? unit.out_accType : partial[row][query];
                updated[row][query] = unit.out_elemType;
                update_enable[row][query] = !reduce && execute &&
                    (mode==PeArrayMode::UPDATE_MAC || unit.out_exp2);
            }
        }
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            output[query] = partial[SA_ROWS][query];
        }
    }

    /**
     * 一套SA_ROWS x SA_COLS PE寄存器依次驻留Q、S/N、P。
     * QK沿feature维归约，CMP逐key更新max，P随后复用同一阵列完成
     * rowsum/PV。QK和PV在tile内部顺序执行，不复制第二套PE网格。
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
        #pragma HLS ALLOCATION function instances=peArrayWave limit=1
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_max type=complete dim=1

        elem_t pe_register[SA_ROWS][SA_COLS]{};
        acc_t scores[SA_COLS][SA_COLS]{};
        elem_t horizontal[SA_ROWS]{};
        acc_t vertical[SA_ROWS][SA_COLS]{};
        bool active[SA_ROWS][SA_COLS]{};
        elem_t wave_register[SA_ROWS][SA_COLS]{};
        bool wave_enable[SA_ROWS][SA_COLS]{};
        elem_t pwl_candidate[exp2PWLPieces][SA_ROWS][SA_COLS]{};
        bool pwl_valid[exp2PWLPieces][SA_ROWS][SA_COLS]{};
        acc_t reduced[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=pe_register type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=scores type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=horizontal type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=vertical type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=active type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=wave_register type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=wave_enable type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pwl_candidate type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pwl_valid type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=reduced type=complete dim=1

        const acc_t idle_intercept = accZero();

        // LOAD_Q：Q token转置为每个PE的stationary寄存器。
        for(int feature=0; feature<SA_ROWS; ++feature){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                pe_register[feature][query] = q_tile[query][feature];
            }
        }

        // AttentionScore：key wave按II=1进入唯一PE网格。
        for(int key=0; key<SA_COLS; ++key){
            #pragma HLS PIPELINE II=1
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS UNROLL
                horizontal[feature] = k_tile[key][feature];
            }
            peArrayWave(
                pe_register, horizontal, vertical, active,
                PeArrayMode::REDUCE, idle_intercept,
                wave_register, wave_enable, reduced
            );
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                scores[query][key] = reduced[query];
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

        // CMP array：每列一个比较器，score以key wave顺序流过。
        for(int key=0; key<SA_COLS; ++key){
            #pragma HLS PIPELINE II=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                if(laneEnabled(meta, query, key)){
                    cmp_max[query] =
                        accCmp(scores[query][key], cmp_max[query]).out_max;
                }
            }
        }

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            result.max_diff[query] =
                previous_max[query]-cmp_max[query];
        }

        // LOAD_S：把score转为元素精度并写入同一组PE.reg。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                active[row][query] = row<SA_COLS &&
                    laneEnabled(meta, query, row);
                pe_register[row][query] = active[row][query]
                    ? cvtAtoE(scores[query][row]) : elemZero();
            }
        }

        // SUB_MAX：每列广播-newMax，使用唯一PE FMA更新reg。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            horizontal[row] = elemOne();
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                vertical[row][query] = -cmp_max[query];
            }
        }
        peArrayWave(
            pe_register, horizontal, vertical, active,
            PeArrayMode::UPDATE_MAC, idle_intercept,
            wave_register, wave_enable, reduced
        );
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                if(wave_enable[row][query]){
                    pe_register[row][query] = wave_register[row][query];
                }
            }
        }

        // SCALE：attentionScale从左侧广播，仍复用同一个PE网格。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            horizontal[row] = elemAttentionScale();
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                vertical[row][query] = accZero();
            }
        }
        peArrayWave(
            pe_register, horizontal, vertical, active,
            PeArrayMode::UPDATE_MAC, idle_intercept,
            wave_register, wave_enable, reduced
        );
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                if(wave_enable[row][query]){
                    pe_register[row][query] = wave_register[row][query];
                }
            }
        }

        // 恢复FSA原始协议：CMP逐拍广播编码intercept，编号保存在其[26:24]。
        // 8拍均读取同一份scaled score快照，只在全部wave返回后选择命中结果，
        // 因而不再在piece循环中形成PE寄存器反馈依赖。
        for(int piece=0; piece<exp2PWLPieces; ++piece){
            #pragma HLS PIPELINE II=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                horizontal[row] = EXP2_SLOPES[piece];
            }
            peArrayWave(
                pe_register, horizontal, vertical, active,
                PeArrayMode::UPDATE_PWL,
                exp2PWLIntercept((exp2_counter_t)piece),
                pwl_candidate[piece], pwl_valid[piece], reduced
            );
        }
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                elem_t probability = elemZero();
                for(int piece=0; piece<exp2PWLPieces; ++piece){
                    #pragma HLS UNROLL
                    if(pwl_valid[piece][row][query]){
                        probability = pwl_candidate[piece][row][query];
                    }
                }
                pe_register[row][query] = active[row][query]
                    ? probability : elemZero();
            }
        }

        // AttentionValue：P留在PE中，wave0求L，后续wave连续求PV。
        for(int key=0; key<SA_ROWS; ++key){
            #pragma HLS UNROLL
            horizontal[key] = elemOne();
        }
        peArrayWave(
            pe_register, horizontal, vertical, active,
            PeArrayMode::REDUCE, idle_intercept,
            wave_register, wave_enable, reduced
        );
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            result.rowsum[query] = reduced[query];
        }

        for(int feature=0; feature<SA_ROWS; ++feature){
            #pragma HLS PIPELINE II=1
            for(int key=0; key<SA_ROWS; ++key){
                #pragma HLS UNROLL
                horizontal[key] = key<SA_COLS
                    ? v_tile[key][feature] : elemZero();
            }
            peArrayWave(
                pe_register, horizontal, vertical, active,
                PeArrayMode::REDUCE, idle_intercept,
                wave_register, wave_enable, reduced
            );
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                result.pv[query][feature] = reduced[query];
            }
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
