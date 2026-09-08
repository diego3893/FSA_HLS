#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    using AccumulatorStorage = BankedSramStorage<
        acc_t, accWidth, ACC_ROWS, SA_COLS, accBanks, ACC_SUB_BANKS
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
        return exp2_mode
            ? (pwl.force_zero ? accZero()
                              : finishAccPwl(result, pwl.integer))
            : result;
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
                acc_t scaled_diff[SA_COLS]{};
                acc_t inverse_l[SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=alpha complete dim=1
                #pragma HLS ARRAY_PARTITION variable=scaled_diff complete dim=1
                #pragma HLS ARRAY_PARTITION variable=inverse_l complete dim=1

                const int alpha_ops = max_token.initialize ? 0 : 2;
                const int update_begin = alpha_ops;
                const int normalize_begin = update_begin+SA_ROWS+1;
                const int arithmetic_ops = normalize_begin+
                    (max_token.finalize ? SA_ROWS : 0);

                // 一个循环、一个调用点就是FSA Accumulator的逐命令复用。
                // alpha两步存在真实前后依赖，因此允许它们等待同一流水单元；
                // 整个Acc阶段仍短于对应SA tile，不成为tile吞吐瓶颈。
                for(int op=0; op<arithmetic_ops; ++op){
                    #pragma HLS LOOP_TRIPCOUNT min=SA_ROWS+1 \
                        max=2*SA_ROWS+3
                    acc_t in_a[SA_COLS]{};
                    acc_t in_b[SA_COLS]{};
                    acc_t in_c[SA_COLS]{};
                    acc_t result[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION variable=in_a complete dim=1
                    #pragma HLS ARRAY_PARTITION variable=in_b complete dim=1
                    #pragma HLS ARRAY_PARTITION variable=in_c complete dim=1
                    #pragma HLS ARRAY_PARTITION variable=result complete dim=1

                    bool exp2_mode = false;
                    unsigned write_address = 0;
                    bool write_result = false;

                    if(op<alpha_ops){
                        exp2_mode = op==1;
                        for(int col=0; col<SA_COLS; ++col){
                            #pragma HLS UNROLL
                            in_a[col] = op==0
                                ? max_token.data[col] : scaled_diff[col];
                            in_b[col] = op==0
                                ? attentionScale() : accZero();
                        }
                    }else if(op<normalize_begin){
                        const unsigned event =
                            (unsigned)(op-update_begin);
                        const SaResultToken value_token =
                            sa_result_stream.read();
                        acc_t old_value[SA_COLS]{};
                        #pragma HLS ARRAY_PARTITION variable=old_value complete dim=1
                        if(!max_token.initialize){
                            bankedSramFullRead<
                                AccumulatorStorage, acc_t, SA_COLS
                            >(storage, event, old_value);
                        }
                        for(int col=0; col<SA_COLS; ++col){
                            #pragma HLS UNROLL
                            in_a[col] = alpha[col];
                            in_b[col] = old_value[col];
                            in_c[col] = value_token.data[col];
                        }
                        write_address = event;
                        write_result = true;
                    }else{
                        const unsigned feature =
                            (unsigned)(op-normalize_begin);
                        if(feature==0){
                            acc_t l_row[SA_COLS]{};
                            #pragma HLS ARRAY_PARTITION variable=l_row complete dim=1
                            bankedSramFullRead<
                                AccumulatorStorage, acc_t, SA_COLS
                            >(storage, 0, l_row);
                            AccumulatorReciprocalColumns<0>::run(
                                l_row, inverse_l
                            );
                        }
                        acc_t old_row[SA_COLS]{};
                        #pragma HLS ARRAY_PARTITION variable=old_row complete dim=1
                        bankedSramFullRead<
                            AccumulatorStorage, acc_t, SA_COLS
                        >(storage, feature+1, old_row);
                        for(int col=0; col<SA_COLS; ++col){
                            #pragma HLS UNROLL
                            in_a[col] = inverse_l[col];
                            in_b[col] = old_row[col];
                        }
                        write_address = feature+1;
                        write_result = true;
                    }

                    accumulatorArithmeticVector(
                        exp2_mode, in_a, in_b, in_c, result
                    );

                    if(op<alpha_ops){
                        for(int col=0; col<SA_COLS; ++col){
                            #pragma HLS UNROLL
                            if(op==0){
                                scaled_diff[col] = result[col];
                            }else{
                                alpha[col] = result[col];
                            }
                        }
                    }else if(write_result){
                        bankedSramFullWrite<
                            AccumulatorStorage, acc_t, SA_COLS
                        >(storage, write_address, result);
                    }
                }

                if(max_token.finalize){

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
