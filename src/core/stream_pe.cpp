#include "fsa/stream_pe.hpp"

#include "fsa/arithmetic.hpp"

namespace fsa{

    void reset_stream_pe_state(StreamPeState& state){
        #pragma HLS INLINE
        state = StreamPeState{};
    }

    StreamPeOutput stream_pe_step(
        StreamPeState& state,
        const StreamPeToken& token
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1

        const bool exp2 = token.op==StreamPeOp::EXP2_PWL;
        const bool mac = token.op==StreamPeOp::QK_MAC ||
            token.op==StreamPeOp::SUB_MAX ||
            token.op==StreamPeOp::SCALE_SCORE ||
            token.op==StreamPeOp::ROWSUM_MAC ||
            token.op==StreamPeOp::PV_MAC;

        // PWL的8个请求可能同时处于FMA流水线中，不能让先返回的分段
        // 结果改变后续请求的输入。pwl_input在整组系数波期间保持不变。
        const elem_t operand_a = exp2 ? state.pwl_input : state.reg;
        const elem_t operand_b = token.horizontal;
        const acc_t operand_c = token.vertical;

        // PE内只有这一个共享FMA入口；opcode只负责选择操作数和提交目标。
        const PeMacUnitOutput arithmetic = peMacUnit(
            operand_a,
            operand_b,
            operand_c,
            exp2
        );

        StreamPeOutput output{};
        output.right = token;
        output.down = token;
        output.down.vertical = mac ? arithmetic.out_accType : token.vertical;

        if(token.valid && token.op==StreamPeOp::LOAD_Q){
            state.reg = token.horizontal;
            state.pwl_input = token.horizontal;
            output.register_written = true;
        }else if(token.valid && token.op==StreamPeOp::LOAD_SCORE){
            state.reg = token.masked ? elemZero() : token.horizontal;
            state.pwl_input = state.reg;
            output.register_written = true;
        }else if(token.valid && token.op==StreamPeOp::SUB_MAX){
            state.reg = arithmetic.out_elemType;
            output.register_written = true;
        }else if(token.valid && token.op==StreamPeOp::SCALE_SCORE){
            state.reg = arithmetic.out_elemType;
            state.pwl_input = state.reg;
            output.register_written = true;
        }else if(token.valid && exp2){
            // 固定8段系数波中恰有一个segment match，因此无需带循环依赖
            // 的exp2Done反馈；这使PWL请求可以保持II=1进入共享FMA。
            if(arithmetic.out_exp2){
                state.reg = arithmetic.out_elemType;
                output.register_written = true;
            }
        }

        output.resident = state.reg;
        return output;
    }

    void stream_pe_process(
        const int instance,
        const elem_t resident,
        const bool lane_enabled,
        const bool reduction,
        const StreamPeOp op,
        const int wave_count,
        StreamPeTokenStream& left,
        StreamPeTokenStream& upward_in,
        StreamPeTokenStream& upward_out,
        StreamPeTokenStream& downward_in,
        StreamPeTokenStream& downward_out,
        StreamPeTokenStream& right,
        StreamPeLaneStream& lane
    ){
        #pragma HLS INLINE off
        #pragma HLS FUNCTION_INSTANTIATE variable=instance

        const int row = instance/SA_COLS;
        const int col = instance%SA_COLS;
        const bool upward = op==StreamPeOp::QK_MAC;

        for(int wave=0; wave<wave_count; ++wave){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT \
                min=1 max=STREAM_MAX_PHASE_WAVES
            const StreamPeToken horizontal = left.read();
            const StreamPeToken vertical = upward
                ? upward_in.read() : downward_in.read();
            const bool operation_valid = horizontal.valid &&
                vertical.valid && (reduction || lane_enabled);
            const bool exp2 = op==StreamPeOp::EXP2_PWL;

            // 左右数据移动不依赖本PE的FMA结果，应当在算术流水线返回前
            // 立即转发。非归约phase的上下数据同样只是广播操作数；提前
            // 转发可避免SUB/SCALE/PWL每跨一个PE都额外串联一次FMA延迟。
            // 归约phase的down.vertical是真正的部分和，仍必须等待FMA。
            right.write(horizontal);
            if(!reduction){
                if(upward){
                    upward_out.write(vertical);
                }else{
                    downward_out.write(vertical);
                }
            }

            StreamPeLaneResult lane_result{};
            if(reduction){
                // 归约phase不使用lane结果，但collector要求每个PE每个wave
                // 都产生一个定长token。先发送bubble，避免无用lane通道
                // 跟随FMA关键路径。
                lane.write(lane_result);
            }

            // LOAD_Q沿左到右路径移动，列号匹配时装入本PE；LOAD_SCORE
            // 来自CMP并沿上到下路径移动，行号匹配时装入同一个reg。
            // 两类搬运不经过FMA，但仍为每个wave产生定长lane token。
            if(op==StreamPeOp::LOAD_Q){
                lane_result.valid = horizontal.valid &&
                    horizontal.tag==(ap_uint<16>)col;
                lane_result.element = horizontal.horizontal;
                lane.write(lane_result);
                continue;
            }
            if(op==StreamPeOp::LOAD_SCORE){
                lane_result.valid = vertical.valid &&
                    vertical.tag==(ap_uint<16>)row;
                lane_result.element = viewAasE(vertical.vertical);
                lane.write(lane_result);
                continue;
            }

            // 所有phase共用该物理PE中的唯一FMA调用点。
            const PeMacUnitOutput arithmetic = peMacUnit(
                resident,
                horizontal.horizontal,
                vertical.vertical,
                exp2
            );

            if(reduction){
                StreamPeToken down_token = vertical;
                if(operation_valid){
                    down_token.vertical = arithmetic.out_accType;
                }
                if(upward){
                    upward_out.write(down_token);
                }else{
                    downward_out.write(down_token);
                }
            }

            if(!reduction){
                lane_result.valid = operation_valid;
                lane_result.segment_match = arithmetic.out_exp2;
                lane_result.element = arithmetic.out_elemType;
                lane.write(lane_result);
            }
        }
    }

}  // namespace fsa
