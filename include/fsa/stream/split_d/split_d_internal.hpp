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

    /**
     * @brief 一次完成一个key tile的整个QK累加（DIM_BLOCKS轮×PE_DIM个特征）。
     *
     * 对应Chisel中QK在阵列上连续跑完dim/D轮才产生完整S的时序：block/lane
     * 循环和operand_b/operand_c的准备都放进本函数，累加值只在函数内部的
     * pe[][].score_acc上传递。这样调用者只需一次调用，PE阵列调用点仍然
     * 唯一（runPeArray只在本函数内部被调用），不会为阶段复制阵列。
     *
     * @param q_tile Q tile，已按col装入query
     * @param k_tile K tile，已按row装入key
     * @param active_queries 本tile有效的query数
     * @param active_keys 本tile有效的key数
     * @param pe PE阵列状态，score_acc进入前必须已清零，返回时保存完整S
     */
    void runPeAccumulateTile(
        const elem_t q_tile[PE_DIM][HEAD_DIM],
        const elem_t k_tile[PE_DIM][HEAD_DIM],
        unsigned active_queries,
        unsigned active_keys,
        PeState pe[PE_DIM][PE_DIM]
    );

    /**
     * @brief 用同一PE阵列把P按行累加，得到一次ROW_SUM。
     *
     * 对应Chisel中SA对概率矩阵做行累加的时序：row循环与operand_b/operand_c
     * 的准备都在本函数内部完成，row_sum只在函数内部逐步累加。与
     * runPeAccumulateTile共用同一个runPeArray调用点。
     *
     * @param pe PE阵列状态，reg保存待累加的P
     * @param row_sum 输出，每列query的P行累加结果
     */
    void runPeRowSum(
        const PeState pe[PE_DIM][PE_DIM],
        acc_t row_sum[PE_DIM]
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
