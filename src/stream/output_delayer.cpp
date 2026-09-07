#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

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

}  // namespace streaming_v2_detail
}  // namespace fsa
