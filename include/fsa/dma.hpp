/**
 * @file dma.hpp
 * @brief 可变长FlashAttention系统顶层使用的64-bit DDR布局和搬运函数
 */
#ifndef DMA_HPP
#define DMA_HPP

#include <ap_int.h>

#include "fsa/config.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /// @brief DDR主口一次传输的数据，宽度与beatBytes一致。
    using dma_word_t = ap_uint<beatBytes*8>;

    constexpr int DMA_ELEMS_PER_WORD = (beatBytes*8)/elemWidth;
    constexpr int DMA_ACCS_PER_WORD = (beatBytes*8)/accWidth;
    constexpr int DMA_QKV_WORDS_PER_ROW = SA_ROWS/DMA_ELEMS_PER_WORD;
    constexpr int DMA_O_WORDS_PER_ROW = SA_ROWS/DMA_ACCS_PER_WORD;
    constexpr int DMA_MAX_QKV_WORDS =
        MAX_SEQUENCE_LENGTH*DMA_QKV_WORDS_PER_ROW;
    constexpr int DMA_MAX_O_WORDS =
        MAX_SEQUENCE_LENGTH*DMA_O_WORDS_PER_ROW;
    constexpr int DMA_MAX_SEQUENCE_TILES =
        (MAX_SEQUENCE_LENGTH+SA_COLS-1)/SA_COLS;

    static_assert(beatBytes==8, "当前DMA顶层固定使用64-bit AXI beat");
    static_assert(elemWidth==16, "当前DMA打包要求FP16 elem_t");
    static_assert(accWidth==32, "当前DMA打包要求FP32 acc_t");
    static_assert(
        SA_ROWS%DMA_ELEMS_PER_WORD==0,
        "每个Q/K/V token必须完整占用若干64-bit beat"
    );
    static_assert(
        SA_ROWS%DMA_ACCS_PER_WORD==0,
        "每个O token必须完整占用若干64-bit beat"
    );

    elem_t dma_unpack_elem(const dma_word_t& word, int lane);
    dma_word_t dma_pack_elem_word(
        const elem_t values[DMA_ELEMS_PER_WORD]
    );
    acc_t dma_unpack_acc(const dma_word_t& word, int lane);
    dma_word_t dma_pack_acc_word(
        const acc_t values[DMA_ACCS_PER_WORD]
    );

    /**
     * @brief 从row-major DDR矩阵读取一个Q/K/V token。
     *
     * memory布局为[L][SA_ROWS] FP16，不要求软件预转置V。
     */
    void dma_load_elem_row(
        const dma_word_t memory[DMA_MAX_QKV_WORDS],
        unsigned row_index,
        elem_t row[SA_ROWS]
    );

    /** @brief 向row-major O矩阵写入一个[SA_ROWS] FP32 token。 */
    void dma_store_acc_row(
        dma_word_t memory[DMA_MAX_O_WORDS],
        unsigned row_index,
        const acc_t row[SA_ROWS]
    );

}  // namespace fsa

#endif  // DMA_HPP
