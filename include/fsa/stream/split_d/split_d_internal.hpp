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
     * @brief 一个PE bank包含的阵列行数。
     *
     * 4×4时整阵列恰好是一个4×4 bank；16×16时是16个1行bank。
     * 该常数只改变同一套PE阵列的RTL层次，不改变PE总数。
     */
    constexpr int PE_BANK_ROWS = 4;
    constexpr int PE_BANK_DIM = PE_DIM < PE_BANK_ROWS ? PE_DIM : PE_BANK_ROWS;

    void peBankMacUnit(
        const PeState pe_row[PE_BANK_DIM],
        const elem_t operand_b_row[PE_BANK_DIM],
        const acc_t operand_c_row[PE_BANK_DIM],
        bool exp2_mode,
        PeMacUnitOutput result_row[PE_BANK_DIM]
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
