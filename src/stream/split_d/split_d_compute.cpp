#include "fsa/stream/split_d/split_d_internal.hpp"

#include "fsa/stream/accumulator.hpp"
#include <utils/x_hls_utils.h>

namespace fsa{
namespace split_d{
namespace detail{

    void stagePeArrayResult(const PeMacUnitOutput input[PE_DIM][QUERY_BLOCK_COLS], PeMacUnitOutput result[PE_DIM][QUERY_BLOCK_COLS]){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1 style=stp
        #pragma HLS LATENCY min=1 max=1
        #pragma HLS ARRAY_PARTITION variable=input complete dim=0
        #pragma HLS ARRAY_PARTITION variable=result complete dim=0

        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS UNROLL
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
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

        PeMacUnitOutput computed[PE_DIM][QUERY_BLOCK_COLS];
        #pragma HLS ARRAY_PARTITION variable=computed complete dim=0
        // 一个空间块为D×B，坐标完全展开；块内六个阶段共享这条求值通路。
        // row/col由UNROLL展开为编译期常量，因此result的写回索引是常量而不是
        // 循环变量——此前的rolled bank形状正是因为写回索引随循环变量变化，
        // 才在写回路径上多出选择逻辑（4×4 7.300→7.893ns、16×16→7.934ns）。
        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS UNROLL
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                computed[row][col] = peMacUnit(
                    pe[row][col].reg,
                    operand_b[row][col],
                    operand_c[row][col],
                    exp2_mode
                );
            }
        }
        // 结果先进入独立寄存器边界，再由引擎写年龄槽；不将存储选择串在FMA返回路径。
        stagePeArrayResult(computed, result);
    }

    /// @brief 按原block/lane顺序完成H个feature的QK；FP32反馈仅属于本块。
    void runPeAccumulateTile(const elem_t q_tile[QUERY_BLOCK_COLS][HEAD_DIM], const elem_t k_tile[PE_DIM][HEAD_DIM], unsigned active_queries, unsigned active_keys, PeState pe[PE_DIM][QUERY_BLOCK_COLS]){
        // 将PE调用放入块内同一作用域，避免helper各自复制一套D×B阵列。
        #pragma HLS INLINE
        #pragma HLS ARRAY_PARTITION variable=q_tile complete dim=1
        #pragma HLS ARRAY_PARTITION variable=k_tile complete dim=1
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0

        // 外层明确保留dim/D轮；每轮依次消费D个特征。
        for(int block=0; block<DIM_BLOCKS; ++block){
            for(int lane=0; lane<PE_DIM; ++lane){
                #pragma HLS PIPELINE II=1
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
        #pragma HLS INLINE
        #pragma HLS ARRAY_PARTITION variable=pe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=row_sum complete dim=1

        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS PIPELINE II=1
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
                    // 只提交当前row的结果；广播部分和，移除PE输入前的行选择器。
                    // 其他行的结果被丢弃，当前行的FMA输入及跨行累加顺序不变。
                    operand_c[r][col] = row_sum[col];
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

    void runSharedEngine(const SharedOperation operation, const unsigned first_context, const unsigned context_count, SharedEngineState& state){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION function instances=runPeArray limit=1
        #pragma HLS ALLOCATION function instances=runAccumulatorColumns limit=1
        #pragma HLS ARRAY_PARTITION variable=state.q complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.q cyclic factor=DMA_ELEMS_PER_WORD dim=3
        #pragma HLS ARRAY_PARTITION variable=state.k complete dim=1
        #pragma HLS ARRAY_PARTITION variable=state.v complete dim=1
        #pragma HLS ARRAY_PARTITION variable=state.reg complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.reg complete dim=3
        #pragma HLS ARRAY_PARTITION variable=state.score complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.score complete dim=3
        #pragma HLS ARRAY_PARTITION variable=state.score_valid complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.score_valid complete dim=3
        #pragma HLS ARRAY_PARTITION variable=state.probability_ready complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.probability_ready complete dim=3
        #pragma HLS ARRAY_PARTITION variable=state.output_acc complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.running_sum complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.next_max complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.alpha_delta complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.alpha complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.inverse_sum complete dim=2

        acc_t pe_acc[PE_DIM][SHARED_PE_HOP][QUERY_BLOCK_COLS];
        elem_t pe_elem[PE_DIM][SHARED_PE_HOP][QUERY_BLOCK_COLS];
        bool pe_hit[PE_DIM][SHARED_PE_HOP][QUERY_BLOCK_COLS];
        SharedToken pe_control[PE_DIM][SHARED_PE_HOP];
        acc_t acc_result[SHARED_ACC_HOP][QUERY_BLOCK_COLS];
        SharedToken acc_control[SHARED_ACC_HOP];
        #pragma HLS ARRAY_PARTITION variable=pe_acc complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pe_elem complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pe_hit complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pe_control complete dim=0
        #pragma HLS ARRAY_PARTITION variable=acc_result complete dim=0
        #pragma HLS ARRAY_PARTITION variable=acc_control complete dim=0

        const bool qk = operation==SharedOperation::QK;
        const bool spatial = operation==SharedOperation::ROW_SUM || operation==SharedOperation::PV;
        const bool vector_pe = qk || operation==SharedOperation::SUB_MAX || operation==SharedOperation::SCALE || operation==SharedOperation::PWL;
        const bool direct_acc = operation==SharedOperation::ALPHA_SCALE || operation==SharedOperation::ALPHA_PWL || operation==SharedOperation::NORMALIZE;
        const unsigned issue_gap = qk && context_count<(unsigned)QK_FEEDBACK_CYCLES
            ? (unsigned)QK_FEEDBACK_CYCLES : context_count;
        const unsigned vector_items = qk ? (unsigned)HEAD_DIM
            : (operation==SharedOperation::PWL ? (unsigned)exp2PWLPieces : 1U);
        const unsigned stream_items = operation==SharedOperation::PV || operation==SharedOperation::NORMALIZE
            ? context_count*(unsigned)HEAD_DIM : context_count;
        const unsigned cycles = vector_pe
            ? (vector_items-1U)*issue_gap+context_count+(unsigned)SHARED_PE_HOP
            : (spatial ? stream_items+(unsigned)(PE_DIM*SHARED_PE_HOP+SHARED_ACC_HOP)
                : stream_items+(unsigned)SHARED_ACC_HOP);
        // operation在有限调用中保持不变，全部结果排空后才切换phase；
        // 因而control只需携带context/feature，不允许跨phase重用未排空环槽。
        unsigned issue_context = 0;
        unsigned issue_index = 0;
        unsigned stream_context = first_context;
        unsigned stream_feature = 0;

        for(unsigned cycle=0; cycle<cycles; ++cycle){
            #pragma HLS PIPELINE II=1
            // 真依赖显式保留：slot读的是HOP拍前写入的结果。
            #pragma HLS DEPENDENCE variable=pe_acc inter RAW distance=FSA_SPLIT_D_PE_HOP_CYCLES true
            #pragma HLS DEPENDENCE variable=pe_elem inter RAW distance=FSA_SPLIT_D_PE_HOP_CYCLES true
            #pragma HLS DEPENDENCE variable=pe_hit inter RAW distance=FSA_SPLIT_D_PE_HOP_CYCLES true
            #pragma HLS DEPENDENCE variable=acc_result inter RAW distance=FSA_SPLIT_D_ACC_HOP_CYCLES true
            #pragma HLS DEPENDENCE variable=state.score inter RAW distance=FSA_SPLIT_D_QK_FEEDBACK_CYCLES true
            // PV/归一化在本次有限调用中每个(context,feature)只读和提交一次。
            // 下一key tile须等待函数排空，不存在本循环内的output_acc RAW反馈。
            #pragma HLS DEPENDENCE variable=state.output_acc inter RAW false
            const unsigned pe_slot = cycle%(unsigned)SHARED_PE_HOP;
            const unsigned acc_slot = cycle%(unsigned)SHARED_ACC_HOP;
            SharedToken row_input[PE_DIM]{};
            #pragma HLS ARRAY_PARTITION variable=row_input complete dim=0
            SharedToken column_input{};

            // 只读取旧环槽。前HOP拍的每个槽先被写入，之后才允许读取。
            if(cycle >= (unsigned)SHARED_ACC_HOP){
                const SharedToken completed = acc_control[acc_slot];
                if(completed.valid){
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                        #pragma HLS UNROLL
                        if(operation==SharedOperation::ALPHA_SCALE || operation==SharedOperation::ALPHA_PWL){
                            state.alpha[completed.context][col] = acc_result[acc_slot][col];
                        }else if(operation==SharedOperation::ROW_SUM){
                            state.running_sum[completed.context][col] = acc_result[acc_slot][col];
                        }else{
                            state.output_acc[completed.context][col][completed.index] = acc_result[acc_slot][col];
                        }
                    }
                }
            }
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                if(cycle >= (unsigned)SHARED_PE_HOP){
                    const SharedToken completed = pe_control[row][pe_slot];
                    if(completed.valid){
                        if(spatial){
                            if(row+1 < PE_DIM){
                                row_input[row+1] = completed;
                            }else{
                                column_input = completed;
                            }
                        }else{
                            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                                #pragma HLS UNROLL
                                const unsigned ctx = completed.context;
                                if(qk){
                                    state.score[ctx][row][col] = pe_acc[row][pe_slot][col];
                                }else if(operation==SharedOperation::SUB_MAX || operation==SharedOperation::SCALE){
                                    state.reg[ctx][row][col] = state.score_valid[ctx][row][col]
                                        ? pe_elem[row][pe_slot][col] : elemZero();
                                }else if(state.score_valid[ctx][row][col] && !state.probability_ready[ctx][row][col] && pe_hit[row][pe_slot][col]){
                                    state.score[ctx][row][col] = (acc_t)pe_elem[row][pe_slot][col];
                                    state.probability_ready[ctx][row][col] = true;
                                }
                            }
                        }
                    }
                }
            }

            if(vector_pe && issue_index<vector_items && issue_context<context_count){
                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    row_input[row] = {true, first_context+issue_context, issue_index};
                }
            }else if(spatial && cycle<stream_items){
                row_input[0] = {true, stream_context, stream_feature};
            }
            if(direct_acc && cycle<stream_items){
                column_input = {true, stream_context, stream_feature};
            }

            PeState operands[PE_DIM][QUERY_BLOCK_COLS];
            elem_t operand_b[PE_DIM][QUERY_BLOCK_COLS];
            acc_t operand_c[PE_DIM][QUERY_BLOCK_COLS];
            PeMacUnitOutput computed[PE_DIM][QUERY_BLOCK_COLS];
            #pragma HLS ARRAY_PARTITION variable=operands complete dim=0
            #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
            #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
            #pragma HLS ARRAY_PARTITION variable=computed complete dim=0
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                const SharedToken token = row_input[row];
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    elem_t a = elemZero();
                    elem_t b = elemZero();
                    acc_t c = accZero();
                    if(token.valid){
                        if(qk){
                            a = state.q[token.context][col][token.index];
                            b = state.k[row][token.index];
                            c = state.score[token.context][row][col];
                        }else{
                            a = state.reg[token.context][row][col];
                            if(spatial){
                                b = operation==SharedOperation::ROW_SUM ? elemOne() : state.v[row][token.index];
                                if(row > 0){
                                    c = pe_acc[row-1][pe_slot][col];
                                }
                            }else if(operation==SharedOperation::SUB_MAX){
                                b = elemOne();
                                c = state.score_valid[token.context][row][col]
                                    ? accSub(accZero(), state.next_max[token.context][col]) : accZero();
                            }else if(operation==SharedOperation::SCALE){
                                b = fp_struct<elem_t>((ap_uint<16>)ATTENTION_SCALE_ELEM_BITS).to_ieee();
                            }else{
                                const elem_t slopes[8] = {(elem_t)0.664062500F, (elem_t)0.608886719F, (elem_t)0.558105469F, (elem_t)0.512207031F, (elem_t)0.469482422F, (elem_t)0.430419922F, (elem_t)0.394775391F, (elem_t)0.362060547F};
                                b = slopes[token.index];
                                c = exp2PWLIntercept((exp2_counter_t)token.index);
                            }
                        }
                    }
                    operands[row][col].reg = a;
                    operand_b[row][col] = b;
                    operand_c[row][col] = c;
                }
            }
            // 唯一坐标求值所有者：全部phase仅选择上述输入，不复制RawFMA。
            runPeArray(operands, operand_b, operand_c, operation==SharedOperation::PWL, computed);

            acc_t acc_a[QUERY_BLOCK_COLS];
            acc_t acc_b[QUERY_BLOCK_COLS];
            acc_t acc_c[QUERY_BLOCK_COLS];
            acc_t acc_computed[QUERY_BLOCK_COLS];
            #pragma HLS ARRAY_PARTITION variable=acc_a complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_b complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_c complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_computed complete dim=1
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                acc_a[col] = accZero();
                acc_b[col] = accZero();
                acc_c[col] = accZero();
                if(column_input.valid){
                    const unsigned ctx = column_input.context;
                    if(operation==SharedOperation::ALPHA_SCALE){
                        acc_a[col] = (acc_t)ATTENTION_SCALE_ACC_VALUE;
                        acc_b[col] = state.alpha_delta[ctx][col];
                    }else if(operation==SharedOperation::ALPHA_PWL){
                        acc_a[col] = state.alpha[ctx][col];
                    }else if(operation==SharedOperation::NORMALIZE){
                        acc_a[col] = state.inverse_sum[ctx][col];
                        acc_b[col] = state.output_acc[ctx][col][column_input.index];
                    }else{
                        acc_a[col] = state.alpha[ctx][col];
                        acc_b[col] = operation==SharedOperation::ROW_SUM
                            ? state.running_sum[ctx][col] : state.output_acc[ctx][col][column_input.index];
                        acc_c[col] = pe_acc[PE_DIM-1][pe_slot][col];
                    }
                }
            }
            // 列Accumulator同样只有一个调用点；ROW_SUM/L先排空，再开始PV。
            runAccumulatorColumns(operation==SharedOperation::ALPHA_PWL, acc_a, acc_b, acc_c, acc_computed);

            // 所有旧槽均已读完，再统一写入新结果/control。
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                pe_control[row][pe_slot] = row_input[row];
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    pe_acc[row][pe_slot][col] = computed[row][col].out_accType;
                    pe_elem[row][pe_slot][col] = computed[row][col].out_elemType;
                    pe_hit[row][pe_slot][col] = computed[row][col].out_exp2;
                }
            }
            acc_control[acc_slot] = column_input;
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                acc_result[acc_slot][col] = acc_computed[col];
            }
            if(vector_pe){
                ++issue_context;
                if(issue_context == issue_gap){
                    issue_context = 0;
                    ++issue_index;
                }
            }
            if((spatial || direct_acc) && cycle<stream_items){
                if(operation==SharedOperation::PV || operation==SharedOperation::NORMALIZE){
                    ++stream_feature;
                    if(stream_feature == (unsigned)HEAD_DIM){
                        stream_feature = 0;
                        ++stream_context;
                    }
                }else{
                    ++stream_context;
                }
            }
        }
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
