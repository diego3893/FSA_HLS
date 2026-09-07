/**
 * @file fsa_streaming_v2.hpp
 * @brief 拆分后的完整序列FSA streaming计算核公共入口。
 */
#ifndef FSA_STREAM_FSA_STREAMING_V2_HPP
#define FSA_STREAM_FSA_STREAMING_V2_HPP

#include <cstdint>

#include <ap_int.h>

#include "fsa/stream/dma.hpp"

namespace fsa{

    enum class FsaStreamingV2Status : std::uint8_t{
        OK = 0,
        INVALID_SEQUENCE_LENGTH = 1
    };

    /**
     * 一次调用完成整个序列attention。调用者只提供Q/K/V/O基地址、
     * 序列长度和causal模式，tile与片上执行时序均由核内部生成。
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

#endif  // FSA_STREAM_FSA_STREAMING_V2_HPP
