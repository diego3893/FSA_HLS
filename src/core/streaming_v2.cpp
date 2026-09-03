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
        acc_t alpha[SA_COLS]{};
        acc_t rowsum[SA_COLS]{};
        acc_t pv[SA_COLS][SA_ROWS]{};
    };

    /**
     * QK阵列完成一个tile后送出的寄存器包。V使用独立stream从
     * Scratchpad直达AttentionValue级，不占用score通道位宽。
     */
    struct ScoreTilePacket{
        TileMeta meta{};
        acc_t scores[SA_COLS][SA_COLS]{};
    };

    /**
     * CMP/PWL级送往AttentionValue阵列的包。probability对应FSA中
     * 回写PE寄存器的P，alpha用于后续Accumulator的跨tile重标定。
     */
    struct ProbabilityTilePacket{
        TileMeta meta{};
        acc_t alpha[SA_COLS]{};
        elem_t probability[SA_ROWS][SA_COLS]{};
    };

    using ElemRowStream = hls::stream<ElemRowPacket>;
    using MetaStream = hls::stream<TileMeta>;
    using ScoreTileStream = hls::stream<ScoreTilePacket>;
    using ProbabilityTileStream = hls::stream<ProbabilityTilePacket>;
    using SaResultStream = hls::stream<SaTileResult>;
    using AccRowStream = hls::stream<AccRowPacket>;

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
     * 一次wave通过完整PE网格。所有调用共享这个禁止内联的调用点，避免
     * HLS为QK、rowsum和PV各复制一套乘加阵列。
     */
    void peArrayReduce(
        const elem_t resident[SA_ROWS][SA_COLS],
        const elem_t horizontal[SA_ROWS],
        acc_t output[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS ARRAY_PARTITION variable=resident type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=horizontal type=complete dim=1
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
                partial[row+1][query] = peMac(
                    resident[row][query],
                    horizontal[row],
                    partial[row][query]
                );
            }
        }
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            output[query] = partial[SA_ROWS][query];
        }
    }

    /**
     * 第一套SA完成QK。它与AttentionValue阵列是两个DATAFLOW级，因此
     * 下一tile的QK可以和上一tile的CMP/PWL、PV并行运行。
     */
    void systolicArrayScoreProcess(
        const unsigned length,
        MetaStream& meta_stream,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ScoreTileStream& score_tile_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION function instances=peArrayReduce limit=1

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                ScoreTilePacket packet{};
                elem_t pe_register[SA_ROWS][SA_COLS]{};
                elem_t k_tile[SA_COLS][SA_ROWS]{};
                #pragma HLS ARRAY_PARTITION variable=packet.scores \
                    type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=pe_register \
                    type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=k_tile \
                    type=complete dim=0

                packet.meta = meta_stream.read();
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS PIPELINE II=1
                    const ElemRowPacket q_packet = q_sa_stream.read();
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        pe_register[feature][query] =
                            q_packet.data[feature];
                    }
                }
                for(int key=0; key<SA_COLS; ++key){
                    #pragma HLS PIPELINE II=1
                    const ElemRowPacket k_packet = k_sa_stream.read();
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        k_tile[key][feature] = k_packet.data[feature];
                    }
                }

                // key wave连续进入QK阵列，阵列内部保持II=1。
                for(int key=0; key<SA_COLS; ++key){
                    #pragma HLS PIPELINE II=1
                    elem_t horizontal[SA_ROWS]{};
                    acc_t reduced[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION variable=horizontal \
                        type=complete dim=1
                    #pragma HLS ARRAY_PARTITION variable=reduced \
                        type=complete dim=1
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        horizontal[feature] = k_tile[key][feature];
                    }
                    peArrayReduce(pe_register, horizontal, reduced);
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        packet.scores[query][key] = reduced[query];
                    }
                }
                score_tile_stream.write(packet);
            }
        }
    }

    /**
     * FSA的CMP、SUB、SCALE和PWL级。cmp_max只在本进程内部反馈，QK
     * 阵列不再被这条跨tile依赖阻塞。
     */
    void softmaxProcess(
        const unsigned length,
        ScoreTileStream& score_tile_stream,
        ProbabilityTileStream& probability_tile_stream
    ){
        #pragma HLS INLINE off

        acc_t cmp_max[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=cmp_max type=complete dim=1

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const ScoreTilePacket score_packet = score_tile_stream.read();
                ProbabilityTilePacket probability_packet{};
                acc_t previous_max[SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=score_packet.scores \
                    type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=probability_packet.alpha \
                    type=complete dim=1
                #pragma HLS ARRAY_PARTITION \
                    variable=probability_packet.probability \
                    type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=previous_max \
                    type=complete dim=1

                probability_packet.meta = score_packet.meta;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    previous_max[query] = cmp_max[query];
                    if(score_packet.meta.initialize){
                        cmp_max[query] = accMinimum();
                    }
                }

                // CMP array：每列一个比较器，score按key wave顺序流过。
                for(int key=0; key<SA_COLS; ++key){
                    #pragma HLS PIPELINE II=1
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        if(laneEnabled(score_packet.meta, query, key)){
                            cmp_max[query] = accCmp(
                                score_packet.scores[query][key],
                                cmp_max[query]
                            ).out_max;
                        }
                    }
                }

                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    probability_packet.alpha[query] =
                        score_packet.meta.initialize
                            ? accZero()
                            : accExp2PWL(
                                (previous_max[query]-cmp_max[query])*
                                attentionScale()
                            );
                }

                // LOAD_S/SUB/SCALE/PWL：生成回写SA的P。
                for(int key=0; key<SA_COLS; ++key){
                    #pragma HLS PIPELINE II=1
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        if(laneEnabled(score_packet.meta, query, key)){
                            const elem_t score = cvtAtoE(
                                score_packet.scores[query][key]
                            );
                            const elem_t normalized = cvtAtoE(peMac(
                                score, elemOne(), -cmp_max[query]
                            ));
                            const elem_t scaled = cvtAtoE(peMac(
                                normalized,
                                elemAttentionScale(),
                                accZero()
                            ));
                            probability_packet.probability[key][query] =
                                cvtAtoE(accExp2PWL((acc_t)scaled));
                        }
                    }
                }

                probability_tile_stream.write(probability_packet);
            }
        }
    }

    /**
     * 第二套SA完成rowsum和PV。QK与PV分成两个并行硬件级，是用约一套
     * PE网格的面积换取tile级流水吞吐；完整计算核仍保持FSA的SA/CMP/
     * Accumulator分层和相同的数值时序。
     */
    void systolicArrayValueProcess(
        const unsigned length,
        ProbabilityTileStream& probability_tile_stream,
        ElemRowStream& v_sa_stream,
        SaResultStream& sa_result_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION function instances=peArrayReduce limit=1

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const ProbabilityTilePacket packet =
                    probability_tile_stream.read();
                SaTileResult result{};
                elem_t v_tile[SA_COLS][SA_ROWS]{};
                elem_t horizontal[SA_ROWS]{};
                acc_t reduced[SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=packet.probability \
                    type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=v_tile \
                    type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=result.alpha \
                    type=complete dim=1
                #pragma HLS ARRAY_PARTITION variable=result.rowsum \
                    type=complete dim=1
                #pragma HLS ARRAY_PARTITION variable=result.pv \
                    type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=horizontal \
                    type=complete dim=1
                #pragma HLS ARRAY_PARTITION variable=reduced \
                    type=complete dim=1

                // V绕过QK/CMP级，在PV阵列入口形成两tile深的弹性旁路。
                for(int key=0; key<SA_COLS; ++key){
                    #pragma HLS PIPELINE II=1
                    const ElemRowPacket v_packet = v_sa_stream.read();
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        v_tile[key][feature] = v_packet.data[feature];
                    }
                }

                for(int key=0; key<SA_ROWS; ++key){
                    #pragma HLS UNROLL
                    horizontal[key] = elemOne();
                }
                peArrayReduce(packet.probability, horizontal, reduced);
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    result.alpha[query] = packet.alpha[query];
                    result.rowsum[query] = reduced[query];
                }

                for(int feature=0; feature<SA_ROWS; ++feature){
                    #pragma HLS PIPELINE II=1
                    for(int key=0; key<SA_ROWS; ++key){
                        #pragma HLS UNROLL
                        horizontal[key] = key<SA_COLS
                            ? v_tile[key][feature] : elemZero();
                    }
                    peArrayReduce(packet.probability, horizontal, reduced);
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        result.pv[query][feature] = reduced[query];
                    }
                }

                result.initialize = packet.meta.initialize;
                result.finalize = packet.meta.finalize;
                result.active_queries = packet.meta.active_queries;
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
                        result.alpha[query],
                        accumulator_sram[0][query],
                        result.rowsum[query]
                    );
                }
                for(int feature=0; feature<SA_ROWS; ++feature){
                    #pragma HLS PIPELINE II=1
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        accumulator_sram[feature+1][query] = accUnit(
                            result.alpha[query],
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

    void dmaWriteO(
        dma_word_t o_address[DMA_MAX_O_WORDS],
        const unsigned length,
        AccRowStream& output_stream,
        ap_uint<8>& status
    ){
        #pragma HLS INLINE off

        for(unsigned query=0; query<length; ++query){
            #pragma HLS PIPELINE II=1
            const AccRowPacket packet = output_stream.read();
            dma_store_acc_row(o_address, query, packet.data);
        }
        status = (ap_uint<8>)static_cast<std::uint8_t>(
            FsaStreamingV2Status::OK
        );
    }

    /**
     * 规范DATAFLOW区域只包含局部stream声明和进程调用。外层参数检查不
     * 再妨碍Vitis把DMA、Scratchpad、两级SA、CMP/PWL和Accumulator
     * 全部抽取成并行进程。
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
        ScoreTileStream score_tile_stream("v2_score_tile");
        ProbabilityTileStream probability_tile_stream("v2_probability_tile");
        SaResultStream sa_result_stream("v2_sa_result");
        AccRowStream output_stream("v2_output");
        #pragma HLS STREAM variable=q_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=meta_stream depth=2
        #pragma HLS STREAM variable=q_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=score_tile_stream depth=2
        #pragma HLS STREAM variable=probability_tile_stream depth=2
        #pragma HLS STREAM variable=sa_result_stream depth=2
        #pragma HLS STREAM variable=output_stream depth=2
        #pragma HLS DATAFLOW

        dmaReadQ(q_address, length, q_dma_stream);
        dmaReadK(k_address, length, k_dma_stream);
        dmaReadV(v_address, length, v_dma_stream);
        scratchpadProcess(
            length, causal,
            q_dma_stream, k_dma_stream, v_dma_stream,
            meta_stream, q_sa_stream, k_sa_stream, v_sa_stream
        );
        systolicArrayScoreProcess(
            length,
            meta_stream, q_sa_stream, k_sa_stream,
            score_tile_stream
        );
        softmaxProcess(
            length, score_tile_stream, probability_tile_stream
        );
        systolicArrayValueProcess(
            length, probability_tile_stream, v_sa_stream, sa_result_stream
        );
        accumulatorProcess(length, sa_result_stream, output_stream);
        dmaWriteO(o_address, length, output_stream, status);
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
