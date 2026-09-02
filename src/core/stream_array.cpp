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

        using PeStream = StreamPeTokenStream;
        using PeLaneResult = StreamPeLaneResult;
        using PeLaneStream = StreamPeLaneStream;

        constexpr int MESH_FIFO_DEPTH = 8;
        static_assert(
            SA_ROWS>=SA_COLS,
            "当前FSA mesh要求head dimension不小于token tile宽度"
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

        /**
         * @brief 全部计算phase复用的唯一FMA mesh。
         *
         * 每个调用的token数只由编译期阵列参数和opcode决定：QK固定
         * SA_COLS，PV固定SA_ROWS，PWL固定8，其余固定1。active_keys只
         * 生成bubble，不再控制循环上界。
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
            elem_t lane_result[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE off
            #pragma HLS DATAFLOW
            #pragma HLS ARRAY_PARTITION variable=resident type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=data type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=column_operand type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=lane_enabled type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=reduction_result type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=lane_result type=complete dim=0

            PeStream horizontal[SA_ROWS][SA_COLS+1];
            PeStream vertical[SA_ROWS+1][SA_COLS];
            PeLaneStream lane[SA_ROWS][SA_COLS];
            #pragma HLS STREAM variable=horizontal depth=MESH_FIFO_DEPTH
            #pragma HLS STREAM variable=vertical depth=MESH_FIFO_DEPTH
            #pragma HLS STREAM variable=lane depth=MESH_FIFO_DEPTH

            stream_input_delayer(
                data, column_operand, active_keys, op, wave_count,
                horizontal, vertical
            );

            #define FSA_STREAM_PE_INSTANCE(ROW, COL) \
                stream_pe_process( \
                    resident[ROW][COL], \
                    lane_enabled[ROW][COL], \
                    reduction, op, wave_count, \
                    horizontal[ROW][COL], vertical[ROW][COL], \
                    horizontal[ROW][COL+1], vertical[ROW+1][COL], \
                    lane[ROW][COL] \
                )

            // 4x4是当前验证和快速综合配置。显式列出调用，使DATAFLOW
            // 区域保持规范形式，并让报告稳定显示16个独立PE任务。
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
                op, wave_count, horizontal, vertical, lane,
                reduction_result, lane_result
            );
        }

        unsigned legacyLogicalStepCount(
            const bool initialize,
            const bool finalize
        ){
            const unsigned reset_steps = initialize ? SA_COLS : 0;
            const unsigned preload_steps =
                (SA_COLS+2*SA_ROWS)*SPAD_SUB_BANKS;
            const unsigned base_instruction_steps =
                (SA_COLS+1)+
                ((2*SA_ROWS+4+exp2PWLPieces-1)+SA_ROWS+SA_COLS+1)+
                (SA_ROWS+SA_COLS+SA_ROWS);
            const unsigned normalization_steps = finalize
                ? (2+reciprocalLatency)+(SA_ROWS+1)
                : 0;
            const unsigned readback_steps = 2*(1+SA_ROWS);
            return reset_steps+preload_steps+base_instruction_steps+
                normalization_steps+readback_steps;
        }

    }  // namespace stream_detail

    void reset_stream_online_state(StreamOnlineState& state){
        #pragma HLS INLINE
        state.active = false;
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            state.m[query] = accMinimum();
            state.l[query] = accZero();
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS UNROLL
                state.o[query][feature] = accZero();
            }
        }
    }

    void stream_fsa_tile(
        StreamOnlineState& online,
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION \
            function instances=stream_detail::runFmaMesh limit=1

        elem_t resident_a[SA_ROWS][SA_COLS]{};
        elem_t resident_b[SA_ROWS][SA_COLS]{};
        elem_t k_tile[SA_COLS][SA_ROWS]{};
        elem_t v_tile[SA_COLS][SA_ROWS]{};
        acc_t mesh_reduction[SA_COLS][SA_ROWS]{};
        acc_t column_operand[SA_COLS]{};
        bool lane_enabled[SA_ROWS][SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=resident_a type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=resident_b type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=mesh_reduction type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=column_operand type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=lane_enabled type=complete dim=0

        // LOAD_Q/LOAD_SCORE只写寄存器，不占用FMA。所有需要乘加的phase
        // 都通过下面同一个runFmaMesh层级执行。
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS PIPELINE II=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                resident_a[row][col] = input.q[col][row];
            }
        }
        for(int key=0; key<SA_COLS; ++key){
            #pragma HLS PIPELINE II=1
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS UNROLL
                k_tile[key][feature] = input.k[key][feature];
                v_tile[key][feature] = input.v[key][feature];
            }
        }

        // DATAFLOW task只接收runFmaMesh的正式参数。mask判断在进入
        // DATAFLOW前完成，避免工具把16个判断表达式变成标量FIFO task。
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

        stream_detail::runFmaMesh(
            resident_a, k_tile, column_operand,
            lane_enabled, input.active_keys,
            StreamPeOp::QK_MAC, SA_COLS, true,
            mesh_reduction, resident_b
        );

        acc_t new_max[SA_COLS]{};
        acc_t max_difference[SA_COLS]{};
        acc_t rowsum[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=new_max type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=max_difference type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=rowsum type=complete dim=1

        // OutputDelayer对齐后的score进入真正的CMP层级。CMP负责mask、
        // 跨tile最大值、max difference以及score反馈bank。
        stream_cmp_update(
            mesh_reduction, input.active_keys, input.causal,
            input.query_base, input.key_base, input.initialize,
            online.m, resident_a, new_max, max_difference
        );
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            column_operand[query] = -new_max[query];
        }

        // 两个phase保留旧实现的FP16舍入边界：先(score-max)写回FP16，
        // 再乘attention scale写回FP16。
        stream_detail::runFmaMesh(
            resident_a, k_tile, column_operand,
            lane_enabled, input.active_keys,
            StreamPeOp::SUB_MAX, 1, false,
            mesh_reduction, resident_b
        );

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            column_operand[query] = accZero();
        }
        stream_detail::runFmaMesh(
            resident_b, k_tile, column_operand,
            lane_enabled, input.active_keys,
            StreamPeOp::SCALE_SCORE, 1, false,
            mesh_reduction, resident_a
        );

        // PWL collector只提交匹配分段；先清零保证masked lane严格为0。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS PIPELINE II=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                resident_b[row][query] = elemZero();
            }
        }
        stream_detail::runFmaMesh(
            resident_a, k_tile, column_operand,
            lane_enabled, input.active_keys,
            StreamPeOp::EXP2_PWL, exp2PWLPieces, false,
            mesh_reduction, resident_b
        );

        stream_detail::runFmaMesh(
            resident_b, k_tile, column_operand,
            lane_enabled, input.active_keys,
            StreamPeOp::ROWSUM_MAC, 1, true,
            mesh_reduction, resident_a
        );
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            rowsum[query] = mesh_reduction[query][0];
        }

        stream_detail::runFmaMesh(
            resident_b, v_tile, column_operand,
            lane_enabled, input.active_keys,
            StreamPeOp::PV_MAC, SA_ROWS, true,
            mesh_reduction, resident_a
        );

        // 独立Accumulator层级保存L/O，并在finalize时使用恢复除法器
        // 每个query只求一次1/L，再用乘法归一化全部feature。
        stream_accumulator_update(
            input.initialize, input.finalize, max_difference,
            rowsum, mesh_reduction, online.l, online.o, output
        );
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            online.m[query] = new_max[query];
        }

        online.active = !input.finalize;
        output.executed_steps = stream_detail::legacyLogicalStepCount(
            input.initialize, input.finalize
        );
        output.request_done = true;
    }

    void fsa_stream_request_run(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        static StreamOnlineState online{};
        #pragma HLS RESET variable=online

        output = FsaCoreRequestOutput{};
        output.request_ready = true;

        if(input.reset){
            reset_stream_online_state(online);
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
        if(!input.initialize && !online.active){
            output.protocol_error = true;
            return;
        }
        if(input.initialize){
            reset_stream_online_state(online);
            online.active = true;
        }

        stream_fsa_tile(online, input, output);
    }

}  // namespace fsa
