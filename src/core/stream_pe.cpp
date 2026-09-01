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

}  // namespace fsa
