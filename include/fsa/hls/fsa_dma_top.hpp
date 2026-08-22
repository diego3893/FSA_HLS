/**
 * @file fsa_dma_top.hpp
 * @brief 外部只提供DDR地址和start的固定tile FlashAttention顶层
 */
#ifndef FSA_DMA_TOP_HPP
#define FSA_DMA_TOP_HPP

#include <cstdint>
#include <ap_int.h>

#include "fsa/dma.hpp"

namespace fsa{

    enum class FsaDmaStatus : std::uint8_t{
        OK = 0,
        CORE_PROTOCOL_ERROR = 1
    };

}  // namespace fsa

/**
 * @brief 串行完成Q/K/VT搬入、单个4x4 FA tile和OL写回。
 *
 * q、k、vt和ol都是由AXI-Lite设置的64-bit DDR基地址。函数返回对应
 * ap_done；status为0表示本次事务成功。
 */
void fsa_dma_top(
    const fsa::dma_word_t q[fsa::DMA_QKV_WORDS],
    const fsa::dma_word_t k[fsa::DMA_QKV_WORDS],
    const fsa::dma_word_t vt[fsa::DMA_QKV_WORDS],
    fsa::dma_word_t ol[fsa::DMA_OL_WORDS],
    bool causal,
    ap_uint<8>& status
);

#endif  // FSA_DMA_TOP_HPP
