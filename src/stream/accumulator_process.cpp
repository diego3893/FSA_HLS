#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    using AccumulatorStorage = BankedSramStorage<
        acc_t, ACC_ROWS, SA_COLS, accBanks, ACC_SUB_BANKS
    >;

    /**
     * 每个COL只有这一处FP32 FMA。普通L/O累加、alpha缩放、PWL以及最终
     * 归一化均通过mode选择输入，避免同一列因多个函数调用点复制运算器。
     */
    template<int COL>
    acc_t accumulatorArithmeticLane(
        const bool exp2_mode,
        const acc_t in_a,
        const acc_t in_b,
        const acc_t in_c
    ){
        static_assert(COL>=0 && COL<SA_COLS,
                      "Accumulator col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=9 max=9

        const AccPwlInput pwl = prepareAccPwlInput(in_a);
        const acc_t result = accUnit(
            exp2_mode ? pwl.fractional : in_a,
            exp2_mode ? pwl.slope : in_b,
            exp2_mode ? pwl.intercept : in_c
        );
        return exp2_mode ? finishAccPwl(result, pwl.integer) : result;
    }

    template<int COL>
    struct AccumulatorArithmeticColumns{
        static void run(
            const bool exp2_mode,
            const acc_t in_a[SA_COLS],
            const acc_t in_b[SA_COLS],
            const acc_t in_c[SA_COLS],
            acc_t output[SA_COLS]
        ){
            #pragma HLS INLINE
            output[COL] = accumulatorArithmeticLane<COL>(
                exp2_mode, in_a[COL], in_b[COL], in_c[COL]
            );
            AccumulatorArithmeticColumns<COL+1>::run(
                exp2_mode, in_a, in_b, in_c, output
            );
        }
    };

    template<>
    struct AccumulatorArithmeticColumns<SA_COLS>{
        static void run(
            const bool,
            const acc_t[SA_COLS],
            const acc_t[SA_COLS],
            const acc_t[SA_COLS],
            acc_t[SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    void accumulatorArithmeticVector(
        const bool exp2_mode,
        const acc_t in_a[SA_COLS],
        const acc_t in_b[SA_COLS],
        const acc_t in_c[SA_COLS],
        acc_t output[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS ARRAY_PARTITION variable=in_a complete dim=1
        #pragma HLS ARRAY_PARTITION variable=in_b complete dim=1
        #pragma HLS ARRAY_PARTITION variable=in_c complete dim=1
        #pragma HLS ARRAY_PARTITION variable=output complete dim=1
        AccumulatorArithmeticColumns<0>::run(
            exp2_mode, in_a, in_b, in_c, output
        );
    }

    template<int COL>
    acc_t accumulatorReciprocalLane(const acc_t denominator){
        static_assert(COL>=0 && COL<SA_COLS,
                      "Accumulator reciprocal col out of range");
        #pragma HLS INLINE off
        return denominator!=accZero()
            ? accumulator_reciprocal(denominator) : accZero();
    }

    template<int COL>
    struct AccumulatorReciprocalColumns{
        static void run(
            const acc_t denominator[SA_COLS],
            acc_t result[SA_COLS]
        ){
            #pragma HLS INLINE
            result[COL] = accumulatorReciprocalLane<COL>(denominator[COL]);
            AccumulatorReciprocalColumns<COL+1>::run(denominator, result);
        }
    };

    template<>
    struct AccumulatorReciprocalColumns<SA_COLS>{
        static void run(const acc_t[SA_COLS], acc_t[SA_COLS]){
            #pragma HLS INLINE
        }
    };

    /**
     * AccRAM采用与FSA-main一致的full-RMW与narrow-DMA读语义。L为地址0，
     * O的feature行位于1..SA_ROWS；地址低位选择bank。
     */
    void accumulatorProcess(
        const unsigned length,
        const bool causal,
        SaResultStream& sa_result_stream,
        DmaWordStream& output_word_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION function \
            instances=accumulatorArithmeticVector limit=1

        AccumulatorStorage storage;
        #pragma HLS BIND_STORAGE variable=storage.data type=ram_t2p impl=bram
        #pragma HLS ARRAY_PARTITION variable=storage.data complete dim=1
        #pragma HLS ARRAY_PARTITION variable=storage.data complete dim=2

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const SaResultToken max_token = sa_result_stream.read();

                acc_t alpha[SA_COLS]{};
                acc_t zeros[SA_COLS]{};
                acc_t scale_value[SA_COLS]{};
                acc_t scaled_diff[SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=alpha complete dim=1
                #pragma HLS ARRAY_PARTITION variable=zeros complete dim=1
                #pragma HLS ARRAY_PARTITION variable=scale_value complete dim=1
                #pragma HLS ARRAY_PARTITION variable=scaled_diff complete dim=1

                if(!max_token.initialize){
                    for(int col=0; col<SA_COLS; ++col){
                        #pragma HLS UNROLL
                        scale_value[col] = attentionScale();
                    }
                    accumulatorArithmeticVector(
                        false, max_token.data, scale_value, zeros,
                        scaled_diff
                    );
                    accumulatorArithmeticVector(
                        true, scaled_diff, zeros, zeros, alpha
                    );
                }

                // event 0更新L，event 1..SA_ROWS更新O各feature行。
                for(int event=0; event<SA_ROWS+1; ++event){
                    #pragma HLS PIPELINE II=1
                    #pragma HLS DEPENDENCE variable=storage.data inter false
                    const SaResultToken value_token =
                        sa_result_stream.read();
                    acc_t old_value[SA_COLS]{};
                    acc_t updated[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION variable=old_value complete dim=1
                    #pragma HLS ARRAY_PARTITION variable=updated complete dim=1
                    if(!max_token.initialize){
                        bankedSramFullRead<
                            AccumulatorStorage, acc_t, SA_COLS
                        >(storage, event, old_value);
                    }
                    accumulatorArithmeticVector(
                        false, alpha, old_value,
                        value_token.data, updated
                    );
                    bankedSramFullWrite<
                        AccumulatorStorage, acc_t, SA_COLS
                    >(storage, event, updated);
                }

                if(max_token.finalize){
                    acc_t l_row[SA_COLS]{};
                    acc_t inverse_l[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION variable=l_row complete dim=1
                    #pragma HLS ARRAY_PARTITION variable=inverse_l complete dim=1
                    bankedSramFullRead<
                        AccumulatorStorage, acc_t, SA_COLS
                    >(storage, 0, l_row);
                    AccumulatorReciprocalColumns<0>::run(l_row, inverse_l);

                    // 用同一组每列FMA原地归一化O，不再生成额外乘法器组。
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS PIPELINE II=1
                        #pragma HLS DEPENDENCE variable=storage.data inter false
                        acc_t old_row[SA_COLS]{};
                        acc_t normalized[SA_COLS]{};
                        #pragma HLS ARRAY_PARTITION variable=old_row complete dim=1
                        #pragma HLS ARRAY_PARTITION variable=normalized complete dim=1
                        bankedSramFullRead<
                            AccumulatorStorage, acc_t, SA_COLS
                        >(storage, feature+1, old_row);
                        accumulatorArithmeticVector(
                            false, inverse_l, old_row, zeros, normalized
                        );
                        bankedSramFullWrite<
                            AccumulatorStorage, acc_t, SA_COLS
                        >(storage, feature+1, normalized);
                    }

                    // AXI是query-major。每个64-bit word从两个feature行执行
                    // narrow-read，并选取当前query所在的sub-bank lane。
                    const int active_queries =
                        max_token.active_queries.to_int();
                    for(int query=0; query<active_queries; ++query){
                        for(int word=0; word<DMA_O_WORDS_PER_ROW; ++word){
                            #pragma HLS PIPELINE II=1
                            acc_t values[DMA_ACCS_PER_WORD]{};
                            #pragma HLS ARRAY_PARTITION variable=values complete dim=1
                            const unsigned sub_bank =
                                (unsigned)query/
                                AccumulatorStorage::SUB_BANK_SIZE;
                            const unsigned lane =
                                (unsigned)query%
                                AccumulatorStorage::SUB_BANK_SIZE;
                            for(int beat_lane=0;
                                    beat_lane<DMA_ACCS_PER_WORD; ++beat_lane){
                                #pragma HLS UNROLL
                                acc_t narrow[
                                    AccumulatorStorage::SUB_BANK_SIZE
                                ];
                                #pragma HLS ARRAY_PARTITION variable=narrow complete dim=1
                                const int feature =
                                    word*DMA_ACCS_PER_WORD+beat_lane;
                                bankedSramNarrowRead(
                                    storage, feature+1, sub_bank, narrow
                                );
                                values[beat_lane] = narrow[lane];
                            }
                            output_word_stream.write(
                                dma_pack_acc_word(values)
                            );
                        }
                    }
                }
            }
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
