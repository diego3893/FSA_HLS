/**
 * @file split_d_internal.hpp
 * @brief Split-D实现内部模块接口。
 */
#ifndef SPLIT_D_INTERNAL_HPP
#define SPLIT_D_INTERNAL_HPP

#include "fsa/stream/dma.hpp"
#include "fsa/stream/split_d/split_d_types.hpp"

namespace fsa{
namespace split_d{
namespace detail{

    constexpr int PV_INTERLEAVE = 8;

    /**
     * @brief 一个PE bank包含的阵列行数与节点个数。
     *
     * bank是同一套PE阵列的RTL层次分组，不改变物理PE总数。每个bank以固定
     * 规模的一维节点数组作为函数实参，因此跨函数边界传递的数组尺寸不随
     * PE_DIM增长，避免大阵列把流水函数的控制流撑到无法调度。
     *
     * 小阵列（PE_DIM<=4）整阵列就是一个bank，保持已验证的单bank结构；
     * 大阵列按行分bank，把跨函数传递的数组规模压到PE_DIM个节点。
     * 可用-DFSA_SPLIT_D_PE_BANK_ROWS=<n>覆盖该选择。
     */
    constexpr int PE_BANK_ROWS =
#ifdef FSA_SPLIT_D_PE_BANK_ROWS
        FSA_SPLIT_D_PE_BANK_ROWS;
#else
        PE_DIM <= 4 ? PE_DIM : 1;
#endif
    constexpr int PE_BANK_NODES =
        (PE_DIM < PE_BANK_ROWS ? PE_DIM : PE_BANK_ROWS) * PE_DIM;

    /**
     * @brief 一次PE阵列求值占用的拍数（即runPeArray的流水间隔）。
     *
     * 4×4时16个PE一拍即可完成，若声明多拍会白白拉长每个调用点，使
     * QK/ROW_SUM的实际II从5退化到10、PWL/PV从1退化到5，因此必须为1。
     * 16×16时256个PE的调度规模远超单拍，需要多拍间隔才能让工具完成
     * 流水调度；QK和ROW_SUM的目标II是5，取5不降低这两个阶段的吞吐。
     */
#ifdef FSA_SPLIT_D_PE_ARRAY_II
    constexpr int PE_ARRAY_II = FSA_SPLIT_D_PE_ARRAY_II;
#else
    constexpr int PE_ARRAY_II = PE_DIM <= 4 ? 1 : 5;
#endif

    void peBankMacUnit(
        const PeState node_pe[PE_BANK_NODES],
        const elem_t node_b[PE_BANK_NODES],
        const acc_t node_c[PE_BANK_NODES],
        bool exp2_mode,
        PeMacUnitOutput node_result[PE_BANK_NODES]
    );

    void runPeArray(
        const PeState pe[PE_DIM][PE_DIM],
        const elem_t operand_b[PE_DIM][PE_DIM],
        const acc_t operand_c[PE_DIM][PE_DIM],
        bool exp2_mode,
        PeMacUnitOutput result[PE_DIM][PE_DIM]
    );

    void runAccumulatorColumns(
        bool exp2_mode,
        const acc_t in_a[PE_DIM],
        const acc_t in_b[PE_DIM],
        const acc_t in_c[PE_DIM],
        acc_t result[PE_DIM]
    );

    void reciprocalColumns(
        const acc_t denominator[PE_DIM],
        acc_t result[PE_DIM]
    );

    void loadElemTile(
        const dma_word_t memory[MAX_QKV_WORDS],
        unsigned token_base,
        unsigned active_tokens,
        elem_t tile[PE_DIM][HEAD_DIM]
    );

    void loadValueTile(
        const dma_word_t memory[MAX_QKV_WORDS],
        unsigned token_base,
        unsigned active_tokens,
        elem_t tile[PE_DIM][HEAD_DIM]
    );

    void storeOutputTile(
        dma_word_t memory[MAX_O_WORDS],
        unsigned token_base,
        unsigned active_tokens,
        const acc_t output[PE_DIM][HEAD_DIM]
    );

}  // namespace detail

    void run(
        const dma_word_t q_address[MAX_QKV_WORDS],
        const dma_word_t k_address[MAX_QKV_WORDS],
        const dma_word_t v_address[MAX_QKV_WORDS],
        dma_word_t o_address[MAX_O_WORDS],
        unsigned length,
        bool causal
    );

}  // namespace split_d
}  // namespace fsa

#endif  // SPLIT_D_INTERNAL_HPP
