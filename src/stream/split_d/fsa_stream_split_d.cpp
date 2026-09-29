#include "fsa/stream/split_d/fsa_stream_split_d.hpp"

#include "fsa/stream/split_d/split_d_internal.hpp"

void fsa_stream_split_d(
    const fsa::dma_word_t
        q_address[fsa::split_d::MAX_QKV_WORDS],
    const fsa::dma_word_t
        k_address[fsa::split_d::MAX_QKV_WORDS],
    const fsa::dma_word_t
        v_address[fsa::split_d::MAX_QKV_WORDS],
    fsa::dma_word_t o_address[fsa::split_d::MAX_O_WORDS],
    const ap_uint<32> sequence_length,
    const bool causal,
    ap_uint<8>& status
){
    #pragma HLS INTERFACE m_axi port=q_address offset=slave bundle=q_gmem \
        depth=FSA_SPLIT_D_DMA_AXI_QKV_DEPTH latency=64 \
        num_read_outstanding=16 max_read_burst_length=64 \
        max_widen_bitwidth=512
    #pragma HLS INTERFACE m_axi port=k_address offset=slave bundle=k_gmem \
        depth=FSA_SPLIT_D_DMA_AXI_QKV_DEPTH latency=64 \
        num_read_outstanding=16 max_read_burst_length=64 \
        max_widen_bitwidth=512
    #pragma HLS INTERFACE m_axi port=v_address offset=slave bundle=v_gmem \
        depth=FSA_SPLIT_D_DMA_AXI_QKV_DEPTH latency=64 \
        num_read_outstanding=16 max_read_burst_length=64 \
        max_widen_bitwidth=512
    #pragma HLS INTERFACE m_axi port=o_address offset=slave bundle=o_gmem \
        depth=FSA_SPLIT_D_DMA_AXI_O_DEPTH latency=64 \
        num_write_outstanding=8 max_write_burst_length=64 \
        max_widen_bitwidth=512
    #pragma HLS INTERFACE s_axilite port=q_address bundle=control
    #pragma HLS INTERFACE s_axilite port=k_address bundle=control
    #pragma HLS INTERFACE s_axilite port=v_address bundle=control
    #pragma HLS INTERFACE s_axilite port=o_address bundle=control
    #pragma HLS INTERFACE s_axilite port=sequence_length bundle=control
    #pragma HLS INTERFACE s_axilite port=causal bundle=control
    #pragma HLS INTERFACE s_axilite port=status bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    const unsigned length = sequence_length.to_uint();
    status = 1;
    if(length==0 || length>(unsigned)fsa::MAX_SEQUENCE_LENGTH){
        return;
    }
    fsa::split_d::run(
        q_address, k_address, v_address, o_address, length, causal
    );
    status = 0;
}
