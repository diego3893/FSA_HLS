#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    /**
     * @brief 显式FSA OutputDelayer协议边界。
     *
     * 当前tagged SA只有在一个完整逻辑结果到达底边时才产生token，列数据
     * 在token内已经对齐。因此这里保持独立OutputDelayer actor，但不再把
     * 每个完整token人为拆成SA_COLS拍再拼回；连续结果可做到token II=1。
     * 若后续把第3项替换成真正逐PE mesh，再把本边界改回逐列valid错拍。
     */
    void outputDelayerProcess(
        const unsigned length,
        const bool causal,
        SaResultStream& raw_result_stream,
        SaResultStream& aligned_result_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                for(int token_index=0;
                        token_index<SA_ROWS+2; ++token_index){
                    #pragma HLS PIPELINE II=1
                    aligned_result_stream.write(raw_result_stream.read());
                }
            }
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
