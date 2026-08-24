#include "fsa/hls/fsa_dma_top.hpp"

#include "fsa/hls/fsa_core_request_top.hpp"

namespace{

    void loadRequestTile(
        const fsa::dma_word_t q[fsa::DMA_MAX_QKV_WORDS],
        const fsa::dma_word_t k[fsa::DMA_MAX_QKV_WORDS],
        const fsa::dma_word_t v[fsa::DMA_MAX_QKV_WORDS],
        const unsigned query_base,
        const unsigned key_base,
        const unsigned length,
        const bool causal,
        fsa::FsaCoreRequestInput& request
    ){
        #pragma HLS INLINE
        #pragma HLS ARRAY_PARTITION variable=request.q type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=request.k type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=request.v type=complete dim=0

        request.reset = query_base==0 && key_base==0;
        request.request_valid = true;
        request.initialize = key_base==0;
        request.finalize = key_base+fsa::SA_COLS>=length;
        request.causal = causal;
        request.query_base = query_base;
        request.key_base = key_base;

        const unsigned remaining_keys = length-key_base;
        request.active_keys = remaining_keys<(unsigned)fsa::SA_COLS
            ? (std::uint16_t)remaining_keys
            : (std::uint16_t)fsa::SA_COLS;

        for(int query_lane=0; query_lane<fsa::SA_COLS; ++query_lane){
            const unsigned query = query_base+(unsigned)query_lane;
            if(query<length){
                fsa::dma_load_elem_row(q, query, request.q[query_lane]);
            }
        }

        // 现有SA的数据通路高度仍为SA_ROWS。矩形配置只在前SA_COLS
        // 行装入真实K/V，其余行保持0，并由ExecutionPlan/CMP显式屏蔽。
        for(int key_lane=0; key_lane<fsa::SA_COLS; ++key_lane){
            const unsigned key = key_base+(unsigned)key_lane;
            if(key<length){
                fsa::dma_load_elem_row(k, key, request.k[key_lane]);
                fsa::dma_load_elem_row(v, key, request.v[key_lane]);
            }
        }
    }

}  // namespace

void fsa_dma_top(
    const fsa::dma_word_t q[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t k[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t v[fsa::DMA_MAX_QKV_WORDS],
    fsa::dma_word_t o[fsa::DMA_MAX_O_WORDS],
    const ap_uint<32> sequence_length,
    const bool causal,
    ap_uint<8>& status
){
    #pragma HLS ALLOCATION function instances=fsa::dma_load_elem_row limit=1
    #pragma HLS ALLOCATION function instances=fsa::dma_store_acc_row limit=1

    #pragma HLS INTERFACE m_axi port=q offset=slave bundle=gmem depth=FSA_DMA_AXI_QKV_DEPTH
    #pragma HLS INTERFACE m_axi port=k offset=slave bundle=gmem depth=FSA_DMA_AXI_QKV_DEPTH
    #pragma HLS INTERFACE m_axi port=v offset=slave bundle=gmem depth=FSA_DMA_AXI_QKV_DEPTH
    #pragma HLS INTERFACE m_axi port=o offset=slave bundle=gmem depth=FSA_DMA_AXI_O_DEPTH

    #pragma HLS INTERFACE s_axilite port=q bundle=control
    #pragma HLS INTERFACE s_axilite port=k bundle=control
    #pragma HLS INTERFACE s_axilite port=v bundle=control
    #pragma HLS INTERFACE s_axilite port=o bundle=control
    #pragma HLS INTERFACE s_axilite port=sequence_length bundle=control
    #pragma HLS INTERFACE s_axilite port=causal bundle=control
    #pragma HLS INTERFACE s_axilite port=status bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    status = (ap_uint<8>)static_cast<std::uint8_t>(
        fsa::FsaDmaStatus::INVALID_SEQUENCE_LENGTH
    );
    const unsigned length = sequence_length.to_uint();
    if(length==0 || length>(unsigned)fsa::MAX_SEQUENCE_LENGTH){
        return;
    }
    status = (ap_uint<8>)static_cast<std::uint8_t>(
        fsa::FsaDmaStatus::CORE_PROTOCOL_ERROR
    );

    // 一次ap_start对应一次完整事务，跨query/KV tile都由本层循环完成。
    for(unsigned query_base=0;
            query_base<length; query_base+=fsa::SA_COLS){
        #pragma HLS LOOP_TRIPCOUNT min=1 max=4096

        for(unsigned key_base=0;
                key_base<length; key_base+=fsa::SA_COLS){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=4096

            fsa::FsaCoreRequestInput request{};
            fsa::FsaCoreRequestOutput response{};
            #pragma HLS ARRAY_PARTITION variable=request.q type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=request.k type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=request.v type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=response.l type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=response.o type=complete dim=0

            loadRequestTile(
                q,
                k,
                v,
                query_base,
                key_base,
                length,
                causal,
                request
            );
            fsa::fsa_core_request_run(request, response);

            if(response.protocol_error || !response.request_done){
                return;
            }
            if(!request.finalize){
                continue;
            }
            if(!response.normalized){
                return;
            }

            for(int query_lane=0;
                    query_lane<fsa::SA_COLS; ++query_lane){
                const unsigned query = query_base+(unsigned)query_lane;
                if(query<length){
                    fsa::dma_store_acc_row(
                        o, query, response.o[query_lane]
                    );
                }
            }
        }
    }

    status = (ap_uint<8>)static_cast<std::uint8_t>(fsa::FsaDmaStatus::OK);
}
