#include "fsa/hls/fsa_dma_top.hpp"

#include "fsa/hls/fsa_core_request_top.hpp"

void fsa_dma_top(
    const fsa::dma_word_t q[fsa::DMA_QKV_WORDS],
    const fsa::dma_word_t k[fsa::DMA_QKV_WORDS],
    const fsa::dma_word_t vt[fsa::DMA_QKV_WORDS],
    fsa::dma_word_t ol[fsa::DMA_OL_WORDS],
    const bool causal,
    ap_uint<8>& status
){
    #pragma HLS INTERFACE m_axi port=q offset=slave bundle=gmem depth=4
    #pragma HLS INTERFACE m_axi port=k offset=slave bundle=gmem depth=4
    #pragma HLS INTERFACE m_axi port=vt offset=slave bundle=gmem depth=4
    #pragma HLS INTERFACE m_axi port=ol offset=slave bundle=gmem depth=10

    #pragma HLS INTERFACE s_axilite port=q bundle=control
    #pragma HLS INTERFACE s_axilite port=k bundle=control
    #pragma HLS INTERFACE s_axilite port=vt bundle=control
    #pragma HLS INTERFACE s_axilite port=ol bundle=control
    #pragma HLS INTERFACE s_axilite port=causal bundle=control
    #pragma HLS INTERFACE s_axilite port=status bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    status = (ap_uint<8>)static_cast<std::uint8_t>(
        fsa::FsaDmaStatus::CORE_PROTOCOL_ERROR
    );

    fsa::FsaCoreRequestInput request{};
    #pragma HLS ARRAY_PARTITION variable=request.q type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=request.k type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=request.v type=complete dim=0

    // 每个外部start都是独立事务，先清除计算核的跨事务状态。
    fsa::FsaCoreRequestInput reset{};
    reset.reset = true;
    fsa::FsaCoreRequestOutput reset_output{};
    fsa::fsa_core_request_run(reset, reset_output);

    fsa::dma_load_qkv(q, k, vt, request.q, request.k, request.v);

    request.request_valid = true;
    request.initialize = true;
    request.finalize = true;
    request.causal = causal;

    fsa::FsaCoreRequestOutput response{};
    #pragma HLS ARRAY_PARTITION variable=response.l type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=response.o type=complete dim=0
    fsa::fsa_core_request_run(request, response);

    if(response.protocol_error || !response.request_done ||
            !response.normalized){
        status = (ap_uint<8>)static_cast<std::uint8_t>(
            fsa::FsaDmaStatus::CORE_PROTOCOL_ERROR
        );
        return;
    }

    fsa::dma_store_ol(ol, response.l, response.o);
    status = (ap_uint<8>)static_cast<std::uint8_t>(fsa::FsaDmaStatus::OK);
}
