/**
 * @file streaming_v2.hpp
 * @brief 一次调用完成完整序列attention的DMA/stream计算核。
 */
#ifndef FSA_STREAMING_V2_HPP
#define FSA_STREAMING_V2_HPP

#include <cstdint>

#include <ap_int.h>

#include "fsa/dma.hpp"

namespace fsa{

    enum class FsaStreamingV2Status : std::uint8_t{
        OK = 0,
        INVALID_SEQUENCE_LENGTH = 1
    };

    /**
     * @brief v2计算核的可复用C/HLS入口。
     *
     * 四个指针在HLS顶层中映射为四组独立m_axi基地址寄存器。调用者只
     * 描述完整矩阵地址、序列长度和causal模式；tile、DMA、SRAM地址与
     * initialize/finalize时序全部由计算核内部产生。
     */
    void fsa_streaming_v2_run(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        const dma_word_t k_address[DMA_MAX_QKV_WORDS],
        const dma_word_t v_address[DMA_MAX_QKV_WORDS],
        dma_word_t o_address[DMA_MAX_O_WORDS],
        ap_uint<32> sequence_length,
        bool causal,
        ap_uint<8>& status
    );

}  // namespace fsa

#endif  // FSA_STREAMING_V2_HPP
