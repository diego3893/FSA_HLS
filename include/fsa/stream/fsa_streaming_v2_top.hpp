/**
 * @file fsa_streaming_v2_top.hpp
 * @brief 拆分后streaming v2的Vitis HLS AXI顶层。
 */
#ifndef FSA_STREAM_FSA_STREAMING_V2_TOP_HPP
#define FSA_STREAM_FSA_STREAMING_V2_TOP_HPP

#include "fsa/stream/fsa_streaming_v2.hpp"

void fsa_streaming_v2_top(
    const fsa::dma_word_t q_address[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t k_address[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t v_address[fsa::DMA_MAX_QKV_WORDS],
    fsa::dma_word_t o_address[fsa::DMA_MAX_O_WORDS],
    ap_uint<32> sequence_length,
    bool causal,
    ap_uint<8>& status
);

#endif  // FSA_STREAM_FSA_STREAMING_V2_TOP_HPP
