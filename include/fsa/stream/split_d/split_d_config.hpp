/**
 * @file split_d_config.hpp
 * @brief Split-D研究顶层的编译期阵列规模和逻辑维度。
 */
#ifndef SPLIT_D_CONFIG_HPP
#define SPLIT_D_CONFIG_HPP

#include <cstdint>

#include "fsa/stream/config.hpp"

#ifndef FSA_SPLIT_D_PE_DIM
#define FSA_SPLIT_D_PE_DIM 4
#endif

#ifndef FSA_SPLIT_D_HEAD_DIM
#define FSA_SPLIT_D_HEAD_DIM 16
#endif

#ifndef FSA_SPLIT_D_DMA_AXI_QKV_DEPTH
#define FSA_SPLIT_D_DMA_AXI_QKV_DEPTH \
    ((FSA_MAX_SEQUENCE_LENGTH*FSA_SPLIT_D_HEAD_DIM)/4)
#endif

#ifndef FSA_SPLIT_D_DMA_AXI_O_DEPTH
#define FSA_SPLIT_D_DMA_AXI_O_DEPTH \
    ((FSA_MAX_SEQUENCE_LENGTH*FSA_SPLIT_D_HEAD_DIM)/2)
#endif

namespace fsa{
namespace split_d{

    /// @brief 方形PE阵列的边长，也是每轮处理的特征数和token数。
    constexpr int PE_DIM = FSA_SPLIT_D_PE_DIM;

    /// @brief 完整Q/K/V向量维度。
    constexpr int HEAD_DIM = FSA_SPLIT_D_HEAD_DIM;

    /// @brief QK和PV需要依次执行的Split-D轮数。
    constexpr int DIM_BLOCKS = HEAD_DIM/PE_DIM;

    constexpr int QKV_ELEMS_PER_WORD = 4;
    constexpr int O_ACCS_PER_WORD = 2;
    constexpr int QKV_WORDS_PER_TOKEN = HEAD_DIM/QKV_ELEMS_PER_WORD;
    constexpr int O_WORDS_PER_TOKEN = HEAD_DIM/O_ACCS_PER_WORD;
    constexpr int MAX_QKV_WORDS =
        MAX_SEQUENCE_LENGTH*QKV_WORDS_PER_TOKEN;
    constexpr int MAX_O_WORDS =
        MAX_SEQUENCE_LENGTH*O_WORDS_PER_TOKEN;
    constexpr int MAX_SEQUENCE_TILES =
        (MAX_SEQUENCE_LENGTH+PE_DIM-1)/PE_DIM;

    static_assert(PE_DIM>0, "Split-D PE维度必须大于0");
    static_assert(HEAD_DIM>0, "Split-D head dimension必须大于0");
    static_assert(HEAD_DIM%PE_DIM==0,
                  "首版Split-D要求HEAD_DIM能被PE_DIM整除");
    static_assert(HEAD_DIM%QKV_ELEMS_PER_WORD==0,
                  "Q/K/V每个token必须完整占用64-bit word");
    static_assert(HEAD_DIM%O_ACCS_PER_WORD==0,
                  "O每个token必须完整占用64-bit word");
    static_assert(PE_DIM<=HEAD_DIM,
                  "PE阵列边长不能大于逻辑head dimension");
    static_assert(HEAD_DIM<=MAX_SUPPORTED_HEAD_DIM,
                  "如需HEAD_DIM大于128，必须同步扩大接口上限");

    constexpr double ATTENTION_SCALE_EXACT =
        1.4426950408889634074/
        detail::compileTimeSqrt((double)HEAD_DIM);
    constexpr float ATTENTION_SCALE_ACC_VALUE =
        static_cast<float>(ATTENTION_SCALE_EXACT);
    constexpr std::uint16_t ATTENTION_SCALE_ELEM_BITS =
        detail::positiveNormalFp16Bits(ATTENTION_SCALE_EXACT);

}  // namespace split_d
}  // namespace fsa

#endif  // SPLIT_D_CONFIG_HPP
