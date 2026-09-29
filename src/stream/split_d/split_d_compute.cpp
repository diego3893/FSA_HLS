#include "fsa/stream/split_d/split_d_internal.hpp"

#include "fsa/stream/accumulator.hpp"

namespace fsa{
namespace split_d{
namespace detail{

    void stagePeArrayResult(
        const PeMacUnitOutput input[PE_DIM][PE_DIM],
        PeMacUnitOutput result[PE_DIM][PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS LATENCY min=1 max=1
        #pragma HLS ARRAY_PARTITION variable=input complete dim=0
        #pragma HLS ARRAY_PARTITION variable=result complete dim=0

        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS UNROLL
            for(int col=0; col<PE_DIM; ++col){
                #pragma HLS UNROLL
                result[row][col] = input[row][col];
            }
        }
    }

    void stageAccumulatorResult(
        const acc_t input[PE_DIM],
        acc_t result[PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS LATENCY min=1 max=1
        #pragma HLS ARRAY_PARTITION variable=input complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result complete dim=1

        for(int col=0; col<PE_DIM; ++col){
            #pragma HLS UNROLL
            result[col] = input[col];
        }
    }

    /**
     * @brief 一个bank内部的一行PE：PE_BANK_DIM个PE并行完成同一拍的RawFMA。
     *
     * 行内完全展开，函数本身保持单拍流水，因此调用它的循环不会把整个
     * 阵列的控制流和数据端口带进自身的流水边界。
     */
    void peBankMacUnit(
        const PeState pe_row[PE_BANK_DIM],
        const elem_t operand_b_row[PE_BANK_DIM],
        const acc_t operand_c_row[PE_BANK_DIM],
        bool exp2_mode,
        PeMacUnitOutput result_row[PE_BANK_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS ARRAY_PARTITION variable=pe_row complete dim=1
        #pragma HLS ARRAY_PARTITION variable=operand_b_row complete dim=1
        #pragma HLS ARRAY_PARTITION variable=operand_c_row complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result_row complete dim=1

        for(int col=0; col<PE_BANK_DIM; ++col){
            #pragma HLS UNROLL
            result_row[col] = peMacUnit(
                pe_row[col].reg,
                operand_b_row[col],
                operand_c_row[col],
                exp2_mode
            );
        }
    }

    /**
     * @brief 层次化调度整套PE阵列：按bank行调用peBankMacUnit。
     *
     * 每个bank只看到自己的PE_BANK_DIM列操作数，因此单个函数的控制流和
     * 端口规模随bank大小固定，不再随PE_DIM增长；bank数量由PE_DIM决定，
     * 4×4时为1个bank，16×16时为16个bank，物理PE总数始终是PE_DIM×PE_DIM。
     */
    void runPeArray(
        const PeState pe[PE_DIM][PE_DIM],
        const elem_t operand_b[PE_DIM][PE_DIM],
        const acc_t operand_c[PE_DIM][PE_DIM],
        const bool exp2_mode,
        PeMacUnitOutput result[PE_DIM][PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
        #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
        #pragma HLS ARRAY_PARTITION variable=result complete dim=0

        for(int bank=0; bank<PE_DIM; bank+=PE_BANK_DIM){
            #pragma HLS UNROLL
            peBankMacUnit(
                pe[bank],
                operand_b[bank],
                operand_c[bank],
                exp2_mode,
                result[bank]
            );
        }
    }

    void runAccumulatorColumns(
        const bool exp2_mode,
        const acc_t in_a[PE_DIM],
        const acc_t in_b[PE_DIM],
        const acc_t in_c[PE_DIM],
        acc_t result[PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS ARRAY_PARTITION variable=in_a complete dim=1
        #pragma HLS ARRAY_PARTITION variable=in_b complete dim=1
        #pragma HLS ARRAY_PARTITION variable=in_c complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result complete dim=1

        acc_t computed[PE_DIM];
        #pragma HLS ARRAY_PARTITION variable=computed complete dim=1

        for(int col=0; col<PE_DIM; ++col){
            #pragma HLS UNROLL
            const AccPwlInput pwl = prepareAccPwlInput(in_a[col]);
            const acc_t operand_a = exp2_mode
                ? pwl.fractional : in_a[col];
            const acc_t operand_b = exp2_mode
                ? pwl.slope : in_b[col];
            const acc_t operand_c = exp2_mode
                ? pwl.intercept : in_c[col];
            const acc_t operation_result = accUnit(
                operand_a, operand_b, operand_c
            );
            computed[col] = exp2_mode
                ? (pwl.force_zero ? accZero()
                    : finishAccPwl(operation_result, pwl.integer))
                : operation_result;
        }
        stageAccumulatorResult(computed, result);
    }

    void reciprocalColumns(
        const acc_t denominator[PE_DIM],
        acc_t result[PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=denominator complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result complete dim=1

        for(int col=0; col<PE_DIM; ++col){
            #pragma HLS UNROLL
            result[col] = denominator[col]!=accZero()
                ? accumulator_reciprocal(denominator[col]) : accZero();
        }
    }

}  // namespace detail
}  // namespace split_d
}  // namespace fsa
