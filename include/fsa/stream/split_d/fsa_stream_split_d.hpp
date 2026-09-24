/**
 * @file fsa_stream_split_d.hpp
 * @brief 参数化D×D Split-D attention HLS顶层。
 */
#ifndef FSA_STREAM_SPLIT_D_HPP
#define FSA_STREAM_SPLIT_D_HPP

#include <ap_int.h>

#include "fsa/stream/dma.hpp"
#include "fsa/stream/split_d/split_d_config.hpp"

void fsa_stream_split_d(
    const fsa::dma_word_t
        q_address[fsa::split_d::MAX_QKV_WORDS],
    const fsa::dma_word_t
        k_address[fsa::split_d::MAX_QKV_WORDS],
    const fsa::dma_word_t
        v_address[fsa::split_d::MAX_QKV_WORDS],
    fsa::dma_word_t o_address[fsa::split_d::MAX_O_WORDS],
    ap_uint<32> sequence_length,
    bool causal,
    ap_uint<8>& status
);

#endif  // FSA_STREAM_SPLIT_D_HPP
