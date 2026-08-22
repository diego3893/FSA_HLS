/**
 * @file dma.hpp
 * @brief 固定4x4 tile的串行DDR搬运接口和数据布局
 *
 * 第一版DMA只追求功能闭环：一个64-bit AXI master按顺序读取Q、K、V转置，
 * 再把L和query-major的O顺序写回。它不实现outstanding、队列或计算重叠。
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

    constexpr int DMA_TILE_ELEMENTS = SA_ROWS*SA_COLS;
    constexpr int DMA_QKV_WORDS =
        DMA_TILE_ELEMENTS/DMA_ELEMS_PER_WORD;

    /// @brief OL中前SA_COLS个FP32保存L，随后保存query-major的O。
    constexpr int DMA_OL_ACC_VALUES =
        SA_COLS+SA_COLS*SA_ROWS;
    constexpr int DMA_OL_WORDS =
        DMA_OL_ACC_VALUES/DMA_ACCS_PER_WORD;

    static_assert(beatBytes==8, "当前DMA顶层固定使用64-bit AXI beat");
    static_assert(elemWidth==16, "当前DMA打包要求FP16 elem_t");
    static_assert(accWidth==32, "当前DMA打包要求FP32 acc_t");
    static_assert(
        SA_ROWS==SA_COLS,
        "当前固定tile DMA要求Q、K、V均为相同的方阵"
    );
    static_assert(
        DMA_TILE_ELEMENTS%DMA_ELEMS_PER_WORD==0,
        "QKV tile必须能完整打包为DMA beat"
    );
    static_assert(
        DMA_OL_ACC_VALUES%DMA_ACCS_PER_WORD==0,
        "OL区域必须能完整打包为DMA beat"
    );

    /** @brief 从64-bit DDR beat中按位解包一个FP16元素。 */
    elem_t dma_unpack_elem(const dma_word_t& word, int lane);

    /** @brief 把四个FP16元素按lane 0在低位的顺序打包为一个DDR beat。 */
    dma_word_t dma_pack_elem_word(
        const elem_t values[DMA_ELEMS_PER_WORD]
    );

    /** @brief 从64-bit DDR beat中按位解包一个FP32累加值。 */
    acc_t dma_unpack_acc(const dma_word_t& word, int lane);

    /** @brief 把两个FP32值按lane 0在低位的顺序打包为一个DDR beat。 */
    dma_word_t dma_pack_acc_word(
        const acc_t values[DMA_ACCS_PER_WORD]
    );

    /**
     * @brief 串行读取Q、K和V转置。
     *
     * Q采用[query][feature]，K采用[key][feature]，DDR中的VT采用
     * [value_feature][key]。v输出转换成请求核所需的[key][value_feature]。
     */
    void dma_load_qkv(
        const dma_word_t q_memory[DMA_QKV_WORDS],
        const dma_word_t k_memory[DMA_QKV_WORDS],
        const dma_word_t vt_memory[DMA_QKV_WORDS],
        elem_t q[SA_COLS][SA_ROWS],
        elem_t k[SA_ROWS][SA_ROWS],
        elem_t v[SA_ROWS][SA_ROWS]
    );

    /**
     * @brief 把L和O写入连续OL区域。
     *
     * 外部布局按FP32索引描述为：
     *   ol[0..SA_COLS-1] = L[query]
     *   ol[SA_COLS + query*SA_ROWS + value_feature] = O[query][value_feature]
     */
    void dma_store_ol(
        dma_word_t ol_memory[DMA_OL_WORDS],
        const acc_t l[SA_COLS],
        const acc_t o[SA_COLS][SA_ROWS]
    );

}  // namespace fsa

#endif  // DMA_HPP
