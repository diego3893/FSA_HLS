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
     * @brief 层次化调度整套PE阵列：按行bank分组，组内完全展开。
     *
     * PE_BANK_ROWS行构成一个bank，bank内PE_BANK_DIM个PE在同一个bank循环体里
     * 原地完全展开；行基址只作为该bank的起始行号，因此单个bank的控制流和
     * 操作数规模固定为PE_BANK_DIM，不随PE_DIM增长。bank数量由PE_DIM决定：
     * 4×4时为1个bank（16个PE），16×16时为16个bank（256个PE），物理PE总数
     * 始终是PE_DIM×PE_DIM，且不为任何阶段复制bank。
     *
     * 函数按PE_ARRAY_II拍接受一次新的阵列求值：4×4的阵列求值本身只需1拍，
     * 16×16时256个PE的调度规模远大于单拍，声明多拍间隔才能让工具完成流水
     * 调度而不报"控制流过于复杂"。QK和ROW_SUM的目标II就是5，因此该间隔不
     * 降低这两个阶段的吞吐。
     */
    void runPeArray(
        const PeState pe[PE_DIM][PE_DIM],
        const elem_t operand_b[PE_DIM][PE_DIM],
        const acc_t operand_c[PE_DIM][PE_DIM],
        const bool exp2_mode,
        PeMacUnitOutput result[PE_DIM][PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=PE_ARRAY_II style=stp
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
        #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
        #pragma HLS ARRAY_PARTITION variable=result complete dim=0

        PeMacUnitOutput computed[PE_DIM][PE_DIM];
        #pragma HLS ARRAY_PARTITION variable=computed complete dim=0

        for(int bank=0; bank<PE_DIM; bank+=PE_BANK_DIM){
            #pragma HLS UNROLL
            for(int offset=0; offset<PE_BANK_DIM; ++offset){
                #pragma HLS UNROLL
                const int row = bank+offset;
                for(int col=0; col<PE_DIM; ++col){
                    #pragma HLS UNROLL
                    computed[row][col] = peMacUnit(
                        pe[row][col].reg, operand_b[row][col],
                        operand_c[row][col], exp2_mode
                    );
                }
            }
        }
        stagePeArrayResult(computed, result);
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
