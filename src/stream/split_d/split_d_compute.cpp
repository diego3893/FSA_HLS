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
     * @brief 一个PE bank：固定PE_BANK_NODES个PE并行完成同一拍RawFMA。
     *
     * 数组形参是固定规模的一维节点数组，函数内部完全展开、自身保持单拍
     * 流水；因此调用它的循环不会把整个PE_DIM×PE_DIM阵列的控制流和数据
     * 端口带进自己的流水边界，跨边界传递的数组尺寸也不随PE_DIM增长。
     */
    void peBankMacUnit(
        const PeState node_pe[PE_BANK_NODES],
        const elem_t node_b[PE_BANK_NODES],
        const acc_t node_c[PE_BANK_NODES],
        bool exp2_mode,
        PeMacUnitOutput node_result[PE_BANK_NODES]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS ARRAY_PARTITION variable=node_pe complete dim=1
        #pragma HLS ARRAY_PARTITION variable=node_b complete dim=1
        #pragma HLS ARRAY_PARTITION variable=node_c complete dim=1
        #pragma HLS ARRAY_PARTITION variable=node_result complete dim=1

        for(int node=0; node<PE_BANK_NODES; ++node){
            #pragma HLS UNROLL
            node_result[node] = peMacUnit(
                node_pe[node].reg,
                node_b[node],
                node_c[node],
                exp2_mode
            );
        }
    }

    /**
     * @brief 层次化调度整套PE阵列：按bank分组，bank内完全展开。
     *
     * PE_BANK_ROWS行构成一个bank，bank内PE_BANK_NODES个PE原地完全展开；
     * 每个bank只以固定规模的一维节点数组调用peBankMacUnit，因此单个bank的
     * 控制流和端口规模固定，不随PE_DIM增长。bank数量由PE_DIM决定：
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

        for(int bank=0; bank<PE_DIM; bank+=PE_BANK_ROWS){
            // 这里刻意不写UNROLL：bank循环必须是流水的迭代维度，每拍只调度
            // 一个bank。若把它展开，所有bank又会合成同一个巨型单拍体，
            // 等于没有拆分——这是此前多次"bank分组"尝试失败的原因。
            //
            // bank结果直接写入result的对应行，不再经过跨迭代共享的中间数组：
            // 官方示例明确说明，未声明依赖时工具会假设存在跨迭代依赖并以更大的
            // II 保守调度（Vitis Accel Examples "Loop Dependency Inter"），
            // 去掉共享中间数组可同时避免保守调度和"部分写入被读走"的竞态。
            PeState node_pe[PE_BANK_NODES];
            elem_t node_b[PE_BANK_NODES];
            acc_t node_c[PE_BANK_NODES];
            PeMacUnitOutput node_result[PE_BANK_NODES];
            #pragma HLS ARRAY_PARTITION variable=node_pe complete dim=1
            #pragma HLS ARRAY_PARTITION variable=node_b complete dim=1
            #pragma HLS ARRAY_PARTITION variable=node_c complete dim=1
            #pragma HLS ARRAY_PARTITION variable=node_result complete dim=1

            for(int offset=0; offset<PE_BANK_NODES; ++offset){
                #pragma HLS UNROLL
                const int row = bank+offset/PE_DIM;
                const int col = offset%PE_DIM;
                node_pe[offset] = pe[row][col];
                node_b[offset] = operand_b[row][col];
                node_c[offset] = operand_c[row][col];
            }
            peBankMacUnit(node_pe, node_b, node_c, exp2_mode, node_result);
            for(int offset=0; offset<PE_BANK_NODES; ++offset){
                #pragma HLS UNROLL
                const int row = bank+offset/PE_DIM;
                const int col = offset%PE_DIM;
                result[row][col] = node_result[offset];
            }
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
