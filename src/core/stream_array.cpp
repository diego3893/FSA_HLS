#include "fsa/stream_array.hpp"

#include <hls_stream.h>

#include "fsa/arithmetic.hpp"
#include "fsa/stream_accumulator.hpp"
#include "fsa/stream_cmp.hpp"
#include "fsa/stream_delayer.hpp"
#include "fsa/stream_pe.hpp"
#include "fsa/stream_types.hpp"

namespace fsa{
    namespace stream_detail{

        constexpr int MESH_FIFO_DEPTH = 16;
        static_assert(
            SA_ROWS>=SA_COLS,
            "FSA mesh要求head dimension不小于token tile宽度"
        );

        bool softmaxLaneEnabled(
            const int row,
            const int col,
            const std::uint16_t active_keys,
            const bool causal,
            const std::uint32_t query_base,
            const std::uint32_t key_base
        ){
            #pragma HLS INLINE
            if(row>=SA_COLS || row>=(int)active_keys){
                return false;
            }
            return !causal ||
                key_base+(unsigned)row<=query_base+(unsigned)col;
        }

        void clearElemMesh(elem_t mesh[SA_ROWS][SA_COLS]){
            #pragma HLS INLINE
            #pragma HLS ARRAY_PARTITION variable=mesh type=complete dim=0
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    mesh[row][col] = elemZero();
                }
            }
        }

        void copyElemMesh(
            const elem_t source[SA_ROWS][SA_COLS],
            elem_t destination[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
            #pragma HLS ARRAY_PARTITION variable=source type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=destination type=complete dim=0
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    destination[row][col] = source[row][col];
                }
            }
        }

#ifndef __SYNTHESIS__
        // C/host仿真不并发执行DATAFLOW task。按数据依赖方向调用同一组
        // stream进程：QK从底到顶，其余phase从顶到下。综合时完全移除
        // 此调度分支，RTL使用下面并发的DATAFLOW网络。
        void simulateFmaMesh(
            const elem_t resident[SA_ROWS][SA_COLS],
            const elem_t data[SA_COLS][SA_ROWS],
            const acc_t column_operand[SA_COLS],
            const bool lane_enabled[SA_ROWS][SA_COLS],
            const std::uint16_t active_keys,
            const StreamPeOp op,
            const int wave_count,
            const bool reduction,
            acc_t reduction_result[SA_COLS][SA_ROWS],
            elem_t lane_result[SA_ROWS][SA_COLS],
            acc_t scalar_reduction[SA_COLS]
        ){
            StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1];
            StreamPeTokenStream upward[SA_ROWS+1][SA_COLS];
            StreamPeTokenStream downward[SA_ROWS+1][SA_COLS];
            StreamPeLaneStream lane[SA_ROWS][SA_COLS];

            stream_input_delayer(
                data, column_operand, active_keys, op, wave_count,
                horizontal, upward, downward
            );

            if(op==StreamPeOp::QK_MAC){
                for(int row=SA_ROWS-1; row>=0; --row){
                    for(int col=0; col<SA_COLS; ++col){
                        stream_pe_process(
                            row*SA_COLS+col, resident[row][col],
                            lane_enabled[row][col], reduction,
                            op, wave_count, horizontal[row][col],
                            upward[row+1][col], upward[row][col],
                            downward[row][col], downward[row+1][col],
                            horizontal[row][col+1], lane[row][col]
                        );
                    }
                }
            }else{
                for(int row=0; row<SA_ROWS; ++row){
                    for(int col=0; col<SA_COLS; ++col){
                        stream_pe_process(
                            row*SA_COLS+col, resident[row][col],
                            lane_enabled[row][col], reduction,
                            op, wave_count, horizontal[row][col],
                            upward[row+1][col], upward[row][col],
                            downward[row][col], downward[row+1][col],
                            horizontal[row][col+1], lane[row][col]
                        );
                    }
                }
            }

            stream_output_delayer(
                op, wave_count, horizontal, upward, downward, lane,
                reduction_result, lane_result, scalar_reduction
            );
        }
#endif

        /**
         * @brief 一套可重入的物理FMA mesh执行一个FSA phase。
         *
         * QK时每个PE从下方读取部分和、向上方发送结果；其余phase从
         * CMP一侧向下移动。runFmaMesh在tile中被顺序调用并限制为一个
         * 实例，因此LOAD/QK/SUB/SCALE/PWL/ROWSUM/PV复用同一组PE。
         */
        void runFmaMesh(
            const elem_t resident[SA_ROWS][SA_COLS],
            const elem_t data[SA_COLS][SA_ROWS],
            const acc_t column_operand[SA_COLS],
            const bool lane_enabled[SA_ROWS][SA_COLS],
            const std::uint16_t active_keys,
            const StreamPeOp op,
            const int wave_count,
            const bool reduction,
            acc_t reduction_result[SA_COLS][SA_ROWS],
            elem_t lane_result[SA_ROWS][SA_COLS],
            acc_t scalar_reduction[SA_COLS]
        ){
            #pragma HLS INLINE off
            #pragma HLS DATAFLOW
            #pragma HLS ARRAY_PARTITION variable=resident type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=column_operand type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=lane_enabled type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=reduction_result type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=lane_result type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=scalar_reduction type=complete dim=1

#ifndef __SYNTHESIS__
            simulateFmaMesh(
                resident, data, column_operand, lane_enabled,
                active_keys, op, wave_count, reduction, reduction_result,
                lane_result, scalar_reduction
            );
            return;
#endif

            StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1];
            StreamPeTokenStream upward[SA_ROWS+1][SA_COLS];
            StreamPeTokenStream downward[SA_ROWS+1][SA_COLS];
            StreamPeLaneStream lane[SA_ROWS][SA_COLS];
            #pragma HLS STREAM variable=horizontal depth=MESH_FIFO_DEPTH
            #pragma HLS STREAM variable=upward depth=MESH_FIFO_DEPTH
            #pragma HLS STREAM variable=downward depth=MESH_FIFO_DEPTH
            #pragma HLS STREAM variable=lane depth=MESH_FIFO_DEPTH

            stream_input_delayer(
                data, column_operand, active_keys, op, wave_count,
                horizontal, upward, downward
            );

            #define FSA_STREAM_PE_INSTANCE(ROW, COL) \
                stream_pe_process( \
                    ROW*SA_COLS+COL, resident[ROW][COL], \
                    lane_enabled[ROW][COL], reduction, op, wave_count, \
                    horizontal[ROW][COL], upward[ROW+1][COL], \
                    upward[ROW][COL], downward[ROW][COL], \
                    downward[ROW+1][COL], horizontal[ROW][COL+1], \
                    lane[ROW][COL] \
                )

            #if FSA_SA_ROWS==4 && FSA_SA_COLS==4
            FSA_STREAM_PE_INSTANCE(0, 0);
            FSA_STREAM_PE_INSTANCE(0, 1);
            FSA_STREAM_PE_INSTANCE(0, 2);
            FSA_STREAM_PE_INSTANCE(0, 3);
            FSA_STREAM_PE_INSTANCE(1, 0);
            FSA_STREAM_PE_INSTANCE(1, 1);
            FSA_STREAM_PE_INSTANCE(1, 2);
            FSA_STREAM_PE_INSTANCE(1, 3);
            FSA_STREAM_PE_INSTANCE(2, 0);
            FSA_STREAM_PE_INSTANCE(2, 1);
            FSA_STREAM_PE_INSTANCE(2, 2);
            FSA_STREAM_PE_INSTANCE(2, 3);
            FSA_STREAM_PE_INSTANCE(3, 0);
            FSA_STREAM_PE_INSTANCE(3, 1);
            FSA_STREAM_PE_INSTANCE(3, 2);
            FSA_STREAM_PE_INSTANCE(3, 3);
            #else
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    FSA_STREAM_PE_INSTANCE(row, col);
                }
            }
            #endif
            #undef FSA_STREAM_PE_INSTANCE

            stream_output_delayer(
                op, wave_count, horizontal, upward, downward, lane,
                reduction_result, lane_result, scalar_reduction
            );
        }

        unsigned logicalStepCount(
            const bool initialize,
            const bool finalize
        ){
            // 外部可见计数继续使用旧FSA请求路径的定义，避免优化改变
            // 软件协议字段；它不是本实现的RTL latency计数器。
            const unsigned reset_steps = initialize ? SA_COLS : 0;
            const unsigned preload_steps =
                (SA_COLS+2*SA_ROWS)*SPAD_SUB_BANKS;
            const unsigned instruction_steps =
                (SA_COLS+1)+
                ((2*SA_ROWS+4+exp2PWLPieces-1)+
                    SA_ROWS+SA_COLS+1)+
                (SA_ROWS+SA_COLS+SA_ROWS);
            const unsigned normalization_steps = finalize
                ? (2+reciprocalLatency)+(SA_ROWS+1)
                : 0;
            const unsigned readback_steps = 2*(1+SA_ROWS);
            return reset_steps+preload_steps+instruction_steps+
                normalization_steps+readback_steps;
        }

    }  // namespace stream_detail

    void stream_fsa_tile(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION \
            function instances=stream_detail::runFmaMesh limit=1

        elem_t pe_register[SA_ROWS][SA_COLS]{};
        elem_t phase_register[SA_ROWS][SA_COLS]{};
        elem_t score_feedback[SA_COLS][SA_ROWS]{};
        acc_t mesh_reduction[SA_COLS][SA_ROWS]{};
        acc_t mesh_scalar_reduction[SA_COLS]{};
        acc_t column_operand[SA_COLS]{};
        bool lane_enabled[SA_ROWS][SA_COLS]{};
        acc_t new_max[SA_COLS]{};
        acc_t max_difference[SA_COLS]{};
        acc_t rowsum[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=pe_register type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=phase_register type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=score_feedback type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=mesh_reduction type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=mesh_scalar_reduction type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=column_operand type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=lane_enabled type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=new_max type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=max_difference type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=rowsum type=complete dim=1

        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                lane_enabled[row][query] =
                    stream_detail::softmaxLaneEnabled(
                        row, query, input.active_keys, input.causal,
                        input.query_base, input.key_base
                    );
            }
        }

        // Q经InputDelayer从左到右移动，每个PE只在列tag命中时装入reg。
        stream_detail::clearElemMesh(phase_register);
        stream_detail::runFmaMesh(
            pe_register, input.q, column_operand, lane_enabled,
            input.active_keys, StreamPeOp::LOAD_Q, SA_COLS, false,
            mesh_reduction, phase_register, mesh_scalar_reduction
        );
        stream_detail::copyElemMesh(phase_register, pe_register);

        // QK部分和沿PE底到顶归约，随后进入4个CMP。
        stream_detail::runFmaMesh(
            pe_register, input.k, column_operand, lane_enabled,
            input.active_keys, StreamPeOp::QK_MAC, SA_COLS, true,
            mesh_reduction, phase_register, mesh_scalar_reduction
        );
        stream_cmp_request(
            input, mesh_reduction, score_feedback,
            new_max, max_difference
        );

        // CMP反馈的score沿顶到下移动，按行tag回到同一套PE的reg。
        stream_detail::clearElemMesh(phase_register);
        stream_detail::runFmaMesh(
            pe_register, score_feedback, column_operand, lane_enabled,
            input.active_keys, StreamPeOp::LOAD_SCORE, SA_ROWS, false,
            mesh_reduction, phase_register, mesh_scalar_reduction
        );
        stream_detail::copyElemMesh(phase_register, pe_register);

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            column_operand[query] = -new_max[query];
        }
        stream_detail::clearElemMesh(phase_register);
        stream_detail::runFmaMesh(
            pe_register, input.k, column_operand, lane_enabled,
            input.active_keys, StreamPeOp::SUB_MAX, 1, false,
            mesh_reduction, phase_register, mesh_scalar_reduction
        );
        stream_detail::copyElemMesh(phase_register, pe_register);

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            column_operand[query] = accZero();
        }
        stream_detail::clearElemMesh(phase_register);
        stream_detail::runFmaMesh(
            pe_register, input.k, column_operand, lane_enabled,
            input.active_keys, StreamPeOp::SCALE_SCORE, 1, false,
            mesh_reduction, phase_register, mesh_scalar_reduction
        );
        stream_detail::copyElemMesh(phase_register, pe_register);

        stream_detail::clearElemMesh(phase_register);
        stream_detail::runFmaMesh(
            pe_register, input.k, column_operand, lane_enabled,
            input.active_keys, StreamPeOp::EXP2_PWL,
            exp2PWLPieces, false,
            mesh_reduction, phase_register, mesh_scalar_reduction
        );
        stream_detail::copyElemMesh(phase_register, pe_register);

        // P留在PE reg中；rowsum和全部PV feature连续通过向下归约路径。
        stream_detail::runFmaMesh(
            pe_register, input.v, column_operand, lane_enabled,
            input.active_keys, StreamPeOp::ROWSUM_PV,
            SA_ROWS+1, true,
            mesh_reduction, phase_register, mesh_scalar_reduction
        );
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            rowsum[query] = mesh_scalar_reduction[query];
        }

        stream_fsa_accumulator_request(
            input, max_difference, rowsum, mesh_reduction, output
        );
    }

    void fsa_stream_request_run(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        static bool online_sequence_active = false;
        #pragma HLS RESET variable=online_sequence_active

        output = FsaCoreRequestOutput{};
        output.request_ready = true;

        if(input.reset){
            online_sequence_active = false;
            if(!input.request_valid){
                return;
            }
        }
        if(!input.request_valid){
            return;
        }
        if(input.active_keys==0 || input.active_keys>SA_COLS){
            output.protocol_error = true;
            return;
        }
        if(!input.initialize && !online_sequence_active){
            output.protocol_error = true;
            return;
        }
        if(input.initialize){
            online_sequence_active = true;
        }

        stream_fsa_tile(input, output);
        output.executed_steps = stream_detail::logicalStepCount(
            input.initialize, input.finalize
        );
        output.request_done = true;
        online_sequence_active = !input.finalize;
    }

}  // namespace fsa
