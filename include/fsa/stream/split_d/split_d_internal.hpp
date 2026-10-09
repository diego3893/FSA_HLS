/**
 * @file split_d_internal.hpp
 * @brief Split-D实现内部模块接口。
 */
#ifndef SPLIT_D_INTERNAL_HPP
#define SPLIT_D_INTERNAL_HPP

#include "fsa/stream/dma.hpp"
#include <hls_stream.h>
#include "fsa/stream/split_d/split_d_types.hpp"

namespace fsa{
namespace split_d{
namespace detail{

    constexpr int PV_INTERLEAVE = 8;

    /**
     * @brief 遗留bank接口的行数与节点个数，当前runPeArray不使用该接口。
     *
     * 本阶段保留死代码，避免混入清理变更；宏不由官方Tcl的环境变量透传，
     * 修改它不能将当前D×B阵列分块，也不证明物理实例或流水可行。
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
     * @brief runPeArray的目标发射间隔，不是一次求值的latency。
     *
     * 已验收未分块4×4的实际II=1、latency=3。阶段3的D×B实例须重新
     * 验收；16×16的遗留默认5尚未验证，不能据此保证PWL/PV II=1。
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
        const PeState pe[PE_DIM][QUERY_BLOCK_COLS],
        const elem_t operand_b[PE_DIM][QUERY_BLOCK_COLS],
        const acc_t operand_c[PE_DIM][QUERY_BLOCK_COLS],
        bool exp2_mode,
        PeMacUnitOutput result[PE_DIM][QUERY_BLOCK_COLS]
    );

    /**
     * @brief 一次完成一个key tile的整个QK累加（DIM_BLOCKS轮×PE_DIM个特征）。
     *
     * 对应Chisel中QK在阵列上连续跑完dim/D轮才产生完整S的时序：block/lane
     * 循环和operand_b/operand_c的准备都放进本函数，累加值只在函数内部的
     * pe[][].score_acc上传递。调用者只需一次调用，PE接口尺寸为D×B；
     * 块内其他阶段也调用runPeArray，实际共享须由RTL验收。
     *
     * @param q_tile Q tile，已按col装入query
     * @param k_tile K tile，已按row装入key
     * @param active_queries 本tile有效的query数
     * @param active_keys 本tile有效的key数
     * @param pe PE阵列状态，score_acc进入前必须已清零，返回时保存完整S
     */
    void runPeAccumulateTile(
        const elem_t q_tile[QUERY_BLOCK_COLS][HEAD_DIM],
        const elem_t k_tile[PE_DIM][HEAD_DIM],
        unsigned active_queries,
        unsigned active_keys,
        PeState pe[PE_DIM][QUERY_BLOCK_COLS]
    );

    /**
     * @brief 用同一PE阵列把P按行累加，得到一次ROW_SUM。
     *
     * 对应Chisel中SA对概率矩阵做行累加的时序：row循环与operand_b/operand_c
     * 的准备都在本函数内部完成，row_sum只在函数内部逐步累加。与
     * runPeAccumulateTile共享同一块内runPeArray实例，不把调用点数当实例数。
     *
     * @param pe PE阵列状态，reg保存待累加的P
     * @param row_sum 输出，每列query的P行累加结果
     */
    void runPeRowSum(
        const PeState pe[PE_DIM][QUERY_BLOCK_COLS],
        acc_t row_sum[QUERY_BLOCK_COLS]
    );

    void runAccumulatorColumns(
        bool exp2_mode,
        const acc_t in_a[QUERY_BLOCK_COLS],
        const acc_t in_b[QUERY_BLOCK_COLS],
        const acc_t in_c[QUERY_BLOCK_COLS],
        acc_t result[QUERY_BLOCK_COLS]
    );

    void reciprocalColumns(
        const acc_t denominator[QUERY_BLOCK_COLS],
        acc_t result[QUERY_BLOCK_COLS]
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
