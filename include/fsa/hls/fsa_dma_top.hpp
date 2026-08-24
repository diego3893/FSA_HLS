/**
 * @file fsa_dma_top.hpp
 * @brief 一次启动完成完整L x head_dim FlashAttention的系统顶层
 */
#ifndef FSA_DMA_TOP_HPP
#define FSA_DMA_TOP_HPP

#include <cstdint>
#include <ap_int.h>

#include "fsa/dma.hpp"

namespace fsa{

    enum class FsaDmaStatus : std::uint8_t{
        OK = 0,
        INVALID_SEQUENCE_LENGTH = 1,
        CORE_PROTOCOL_ERROR = 2
    };

}  // namespace fsa

/**
 * @brief 从DDR读取row-major Q/K/V，一次事务产生完整row-major O。
 *
 * Q、K、V为[sequence_length][SA_ROWS] FP16，O为同形状FP32。
 * SA_ROWS是head dimension，SA_COLS是序列tile大小。L只通过AXI-Lite
 * 标量端口输入，不会写回DDR。
 */
void fsa_dma_top(
    const fsa::dma_word_t q[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t k[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t v[fsa::DMA_MAX_QKV_WORDS],
    fsa::dma_word_t o[fsa::DMA_MAX_O_WORDS],
    ap_uint<32> sequence_length,
    bool causal,
    ap_uint<8>& status
);

#endif  // FSA_DMA_TOP_HPP
