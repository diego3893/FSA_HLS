// TODO: #pragma 修饰是必须的！级间寄存器partition以及循环unroll
#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"

namespace fsa{

    void reset_accumulator_state(AccumulatorState& state){
        for(int col=0; col<SA_COLS; ++col){
            state.scale[col] = accZero();
            state.reciprocal_busy[col] = false;
            state.reciprocal_counter[col] = 0;
            state.reciprocal_operand[col] = accZero();
        }
        return;
    }

    void accumulator_step(const AccumulatorState& current,
                        AccumulatorState& next, AccumulatorIO& io){
        next = current;

        const bool valid = io.ctrl_in.valid;
        const AccumulatorCmd cmd = io.ctrl_in.bits.cmd;

        const bool exp_s1 = cmd == AccumulatorCmd::EXP_S1;
        const bool exp_s2 = cmd == AccumulatorCmd::EXP_S2;
        const bool acc_sa = cmd == AccumulatorCmd::ACC_SA;
        const bool set = cmd == AccumulatorCmd::SET_SCALE;
        const bool reciprocal_cmd = cmd == AccumulatorCmd::RECIPROCAL;

        for(int col=0; col<SA_COLS; ++col){
            const acc_t in_a = exp_s1 ? io.sa_in[(std::size_t)col]
                                    : current.scale[col];
            const acc_t in_b = exp_s1 ? attentionScale()
                                    : io.sram_in[(std::size_t)col];
            const acc_t in_c = acc_sa ? io.sa_in[(std::size_t)col]
                                    : accZero();

            acc_t unit_output = exp_s2 ? accExp2PWL(in_a)
                                    : accUnit(in_a, in_b, in_c);
            bool reciprocal_out_valid = false;

            // 多拍除法处理
            // TODO: 现在的实现是dummy，除法器的多拍调用没有实现
            if(current.reciprocal_busy[col]){
                if(current.reciprocal_counter[col] <= 1){ 
                    // 最后一拍计算结束，保存并标记有效
                    reciprocal_out_valid = true;
                    unit_output = reciprocal(current.reciprocal_operand[col]);
                    next.reciprocal_busy[col] = false;
                    next.reciprocal_counter[col] = 0;
                }else{
                    next.reciprocal_counter[col] = 
                        current.reciprocal_counter[col]-1;
                }
            }else if(valid && reciprocal_cmd){
                next.reciprocal_busy[col] = true;
                next.reciprocal_counter[col] = 
                    (reciprocal_counter_t)(reciprocalLatency-1);
                next.reciprocal_operand[col] = current.scale[col];
            }

            io.sram_out[(std::size_t)col] = unit_output;

            if(valid){
                if(exp_s1 || exp_s2 || reciprocal_out_valid){
                    next.scale[col] = unit_output;
                }else if(set){
                    next.scale[col] = io.sram_in[(std::size_t)col];
                }
            }
        }
    }

}  // namespace fsa
