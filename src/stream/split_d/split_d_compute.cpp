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

        // P3：不再按bank循环调用，而是让整个阵列在同一个流水体内原地求值。
        // row/col由UNROLL展开为编译期常量，因此result的写回索引是常量而不是
        // 循环变量——此前的rolled bank形状正是因为写回索引随循环变量变化，
        // 才在写回路径上多出选择逻辑（4×4 7.300→7.893ns、16×16→7.934ns）。
        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS UNROLL
            for(int col=0; col<PE_DIM; ++col){
                #pragma HLS UNROLL
                result[row][col] = peMacUnit(
                    pe[row][col].reg,
                    operand_b[row][col],
                    operand_c[row][col],
                    exp2_mode
                );
            }
        }
    }

    /**
     * @brief 一个key tile的QK累加全部在本函数内完成，对外只暴露一次调用。
     *
     * 上一版把block/lane循环留在控制器里，每次lane迭代都跨函数边界调用
     * runPeArray，并用控制器的共享数组operand_c传递"下一次累加值"，于是
     * operand_c在相邻迭代之间形成store→load的跨迭代依赖，工具只能报
     * HLS 200-880/200-875并把II压到8。这里把循环体和数组都收进函数内部：
     * operand_b/operand_c作为本迭代私有的局部数组，只在runPeArray调用前
     * 被写、调用中被读，跨迭代不再有共享数组；pe[][].score_acc的RAW则被
     * 一次阵列求值（PE_ARRAY_II拍）完全吸收在流水体内。
     *
     * 单次runPeArray调用点仍然只有一个，因此不会为阶段复制PE阵列。
     */
    void runPeAccumulateTile(
        const elem_t q_tile[PE_DIM][HEAD_DIM],
        const elem_t k_tile[PE_DIM][HEAD_DIM],
        unsigned active_queries,
        unsigned active_keys,
        PeState pe[PE_DIM][PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=q_tile complete dim=1
        #pragma HLS ARRAY_PARTITION variable=k_tile complete dim=1
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0

        // 外层明确保留dim/D轮；每轮依次消费D个特征。
        for(int block=0; block<DIM_BLOCKS; ++block){
            for(int lane=0; lane<PE_DIM; ++lane){
                #pragma HLS PIPELINE II=5
                const int feature = block*PE_DIM+lane;
                elem_t operand_b[PE_DIM][PE_DIM];
                acc_t operand_c[PE_DIM][PE_DIM];
                PeMacUnitOutput pe_result[PE_DIM][PE_DIM];
                #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
                #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
                #pragma HLS ARRAY_PARTITION variable=pe_result complete dim=0

                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<PE_DIM; ++col){
                        #pragma HLS UNROLL
                        pe[row][col].reg =
                            (unsigned)col<active_queries
                                ? q_tile[col][feature] : elemZero();
                        operand_b[row][col] =
                            (unsigned)row<active_keys
                                ? k_tile[row][feature] : elemZero();
                        operand_c[row][col] =
                            pe[row][col].score_acc;
                    }
                }
                runPeArray(
                    pe, operand_b, operand_c, false, pe_result
                );
                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<PE_DIM; ++col){
                        #pragma HLS UNROLL
                        pe[row][col].score_acc =
                            pe_result[row][col].out_accType;
                    }
                }
            }
        }
    }

    /**
     * @brief 用同一PE阵列逐行累加P，得到一次ROW_SUM。
     *
     * 与runPeAccumulateTile同样把row循环和operand_b/operand_c收进函数：
     * row_sum的跨行累加只发生在函数内部的局部数组上，runPeArray在相邻行
     * 之间不再通过控制器的共享数组传递数据。累加顺序仍是row=0..PE_DIM-1，
     * 每行的部分和累加到对应列，结果与逐行调用时完全一致。
     */
    void runPeRowSum(
        const PeState pe[PE_DIM][PE_DIM],
        acc_t row_sum[PE_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=row_sum complete dim=1

        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS PIPELINE II=5
            elem_t operand_b[PE_DIM][PE_DIM];
            acc_t operand_c[PE_DIM][PE_DIM];
            PeMacUnitOutput pe_result[PE_DIM][PE_DIM];
            #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
            #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
            #pragma HLS ARRAY_PARTITION variable=pe_result complete dim=0

            for(int r=0; r<PE_DIM; ++r){
                #pragma HLS UNROLL
                for(int col=0; col<PE_DIM; ++col){
                    #pragma HLS UNROLL
                    operand_b[r][col] = elemOne();
                    operand_c[r][col] = r==row
                        ? row_sum[col] : accZero();
                }
            }
            runPeArray(
                pe, operand_b, operand_c, false, pe_result
            );
            for(int col=0; col<PE_DIM; ++col){
                #pragma HLS UNROLL
                row_sum[col] =
                    pe_result[row][col].out_accType;
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
