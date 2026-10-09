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

    void stageAccumulatorResult(const acc_t input[QUERY_BLOCK_COLS], acc_t result[QUERY_BLOCK_COLS]){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS LATENCY min=1 max=1
        #pragma HLS ARRAY_PARTITION variable=input complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result complete dim=1

        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
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

    /// @brief 一个D×B空间块的共享RawFMA阵列，D为key方向，B为query列。
    void runPeArray(const PeState pe[PE_DIM][QUERY_BLOCK_COLS], const elem_t operand_b[PE_DIM][QUERY_BLOCK_COLS], const acc_t operand_c[PE_DIM][QUERY_BLOCK_COLS], const bool exp2_mode, PeMacUnitOutput result[PE_DIM][QUERY_BLOCK_COLS]){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=PE_ARRAY_II style=stp
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
        #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
        #pragma HLS ARRAY_PARTITION variable=result complete dim=0

        // 一个空间块为D×B，坐标完全展开；块内六个阶段共享这条求值通路。
        // row/col由UNROLL展开为编译期常量，因此result的写回索引是常量而不是
        // 循环变量——此前的rolled bank形状正是因为写回索引随循环变量变化，
        // 才在写回路径上多出选择逻辑（4×4 7.300→7.893ns、16×16→7.934ns）。
        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS UNROLL
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
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

    /// @brief 按原block/lane顺序完成H个feature的QK；FP32反馈仅属于本块。
    void runPeAccumulateTile(const elem_t q_tile[QUERY_BLOCK_COLS][HEAD_DIM], const elem_t k_tile[PE_DIM][HEAD_DIM], unsigned active_queries, unsigned active_keys, PeState pe[PE_DIM][QUERY_BLOCK_COLS]){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=q_tile complete dim=1
        #pragma HLS ARRAY_PARTITION variable=k_tile complete dim=1
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0

        // 外层明确保留dim/D轮；每轮依次消费D个特征。
        for(int block=0; block<DIM_BLOCKS; ++block){
            for(int lane=0; lane<PE_DIM; ++lane){
                #pragma HLS PIPELINE II=5
                const int feature = block*PE_DIM+lane;
                elem_t operand_b[PE_DIM][QUERY_BLOCK_COLS];
                acc_t operand_c[PE_DIM][QUERY_BLOCK_COLS];
                PeMacUnitOutput pe_result[PE_DIM][QUERY_BLOCK_COLS];
                #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
                #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
                #pragma HLS ARRAY_PARTITION variable=pe_result complete dim=0

                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
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
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
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
    void runPeRowSum(const PeState pe[PE_DIM][QUERY_BLOCK_COLS], acc_t row_sum[QUERY_BLOCK_COLS]){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=row_sum complete dim=1

        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS PIPELINE II=5
            elem_t operand_b[PE_DIM][QUERY_BLOCK_COLS];
            acc_t operand_c[PE_DIM][QUERY_BLOCK_COLS];
            PeMacUnitOutput pe_result[PE_DIM][QUERY_BLOCK_COLS];
            #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
            #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
            #pragma HLS ARRAY_PARTITION variable=pe_result complete dim=0

            for(int r=0; r<PE_DIM; ++r){
                #pragma HLS UNROLL
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    operand_b[r][col] = elemOne();
                    operand_c[r][col] = r==row
                        ? row_sum[col] : accZero();
                }
            }
            runPeArray(
                pe, operand_b, operand_c, false, pe_result
            );
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                row_sum[col] =
                    pe_result[row][col].out_accType;
            }
        }
    }

    void runAccumulatorColumns(const bool exp2_mode, const acc_t in_a[QUERY_BLOCK_COLS], const acc_t in_b[QUERY_BLOCK_COLS], const acc_t in_c[QUERY_BLOCK_COLS], acc_t result[QUERY_BLOCK_COLS]){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS ARRAY_PARTITION variable=in_a complete dim=1
        #pragma HLS ARRAY_PARTITION variable=in_b complete dim=1
        #pragma HLS ARRAY_PARTITION variable=in_c complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result complete dim=1

        acc_t computed[QUERY_BLOCK_COLS];
        #pragma HLS ARRAY_PARTITION variable=computed complete dim=1

        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
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

    void reciprocalColumns(const acc_t denominator[QUERY_BLOCK_COLS], acc_t result[QUERY_BLOCK_COLS]){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=denominator complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result complete dim=1

        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
            #pragma HLS UNROLL
            result[col] = denominator[col]!=accZero()
                ? accumulator_reciprocal(denominator[col]) : accZero();
        }
    }

}  // namespace detail
}  // namespace split_d
}  // namespace fsa
