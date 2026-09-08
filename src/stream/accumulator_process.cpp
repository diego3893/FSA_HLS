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

    /**
     * 独立DATAFLOW actor是整核中唯一的Accumulator向量算术调用点。
     * 请求来自alpha生成、L/O更新和最终归一化，但在这里统一经过同一组
     * C列FP32 MAC/exp2 lane，防止Vitis按调用上下文克隆第二套阵列。
     */
    void accumulatorArithmeticProcess(
        const unsigned length,
        const bool causal,
        AccArithmeticRequestStream& request_stream,
        AccArithmeticResponseStream& response_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        const unsigned tile_visits = causal
            ? tiles*(tiles+1U)/2U : tiles*tiles;
        const unsigned request_count =
            2U*(tile_visits-tiles)
            + tile_visits*(unsigned)(SA_ROWS+1)
            + tiles*(unsigned)SA_ROWS;

        for(unsigned request_index=0;
                request_index<request_count; ++request_index){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=2*SA_ROWS+1 \
                max=MAX_ACC_ARITHMETIC_REQUESTS
            const AccArithmeticRequest request = request_stream.read();
            AccArithmeticResponse response{};
            accumulatorArithmeticVector(
                request.exp2_mode,
                request.in_a, request.in_b, request.in_c,
                response.data
            );
            response_stream.write(response);
        }
    }

    void requestAccumulatorArithmetic(
        AccArithmeticRequestStream& request_stream,
        AccArithmeticResponseStream& response_stream,
        const bool exp2_mode,
        const acc_t in_a[SA_COLS],
        const acc_t in_b[SA_COLS],
        const acc_t in_c[SA_COLS]
    ){
        #pragma HLS INLINE
        AccArithmeticRequest request{};
        request.exp2_mode = exp2_mode;
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            request.in_a[col] = in_a[col];
            request.in_b[col] = in_b[col];
            request.in_c[col] = in_c[col];
        }
        #ifdef __SYNTHESIS__
        request_stream.write(request);
        #else
        // 普通C/C++仿真顺序执行DATAFLOW函数，无法模拟请求/响应环的
        // 并发actor。仿真时在此执行同一个算术函数；综合和RTL协同时
        // 则只走stream，由唯一accumulatorArithmeticProcess实现硬件。
        AccArithmeticResponse response{};
        accumulatorArithmeticVector(
            request.exp2_mode,
            request.in_a, request.in_b, request.in_c,
            response.data
        );
        response_stream.write(response);
        #endif
    }

    void receiveAccumulatorArithmetic(
        AccArithmeticResponseStream& response_stream,
        acc_t output[SA_COLS]
    ){
        #pragma HLS INLINE
        const AccArithmeticResponse response = response_stream.read();
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            output[col] = response.data[col];
        }
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
        AccArithmeticRequestStream& arithmetic_request_stream,
        AccArithmeticResponseStream& arithmetic_response_stream,
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
                    requestAccumulatorArithmetic(
                        arithmetic_request_stream,
                        arithmetic_response_stream,
                        false, max_token.data, scale_value, zeros
                    );
                    receiveAccumulatorArithmetic(
                        arithmetic_response_stream, scaled_diff
                    );
                    requestAccumulatorArithmetic(
                        arithmetic_request_stream,
                        arithmetic_response_stream,
                        true, scaled_diff, zeros, zeros
                    );
                    receiveAccumulatorArithmetic(
                        arithmetic_response_stream, alpha
                    );
                }

                // 先连续发出L/O更新，再按同一顺序接收并写回。算术actor
                // 可以II=1接收整批请求，同时避免控制进程逐项等待FMA延迟。
                for(int event=0; event<SA_ROWS+1; ++event){
                    #pragma HLS PIPELINE II=1
                    const SaResultToken value_token =
                        sa_result_stream.read();
                    acc_t old_value[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION variable=old_value complete dim=1
                    if(!max_token.initialize){
                        bankedSramFullRead<
                            AccumulatorStorage, acc_t, SA_COLS
                        >(storage, event, old_value);
                    }
                    requestAccumulatorArithmetic(
                        arithmetic_request_stream,
                        arithmetic_response_stream,
                        false, alpha, old_value, value_token.data
                    );
                }
                for(int event=0; event<SA_ROWS+1; ++event){
                    #pragma HLS PIPELINE II=1
                    #pragma HLS DEPENDENCE variable=storage.data inter false
                    acc_t updated[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION variable=updated complete dim=1
                    receiveAccumulatorArithmetic(
                        arithmetic_response_stream, updated
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

                    // 连续送出所有O/L请求，统一复用算术actor中的C路MAC。
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS PIPELINE II=1
                        acc_t old_row[SA_COLS]{};
                        #pragma HLS ARRAY_PARTITION variable=old_row complete dim=1
                        bankedSramFullRead<
                            AccumulatorStorage, acc_t, SA_COLS
                        >(storage, feature+1, old_row);
                        requestAccumulatorArithmetic(
                            arithmetic_request_stream,
                            arithmetic_response_stream,
                            false, inverse_l, old_row, zeros
                        );
                    }
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS PIPELINE II=1
                        #pragma HLS DEPENDENCE variable=storage.data inter false
                        acc_t normalized[SA_COLS]{};
                        #pragma HLS ARRAY_PARTITION variable=normalized complete dim=1
                        receiveAccumulatorArithmetic(
                            arithmetic_response_stream, normalized
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
