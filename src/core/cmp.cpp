#include "fsa/cmp.hpp"
#include "fsa/arithmetic.hpp"

namespace fsa{

    void reset_cmp_state(CMPState& state){
        state.oldMax = accMinimum();
        state.newMax = accMinimum();
        state.exp2_counter = 0;
        return;
    }

    void cmp_step(const CMPState& current, CMPState& next, CMPIO& io){
        #pragma HLS INLINE
        next = current;

        const CmpControlCmd cmd = io.in_ctrl.bits.cmd;
        const bool fire = io.in_ctrl.valid;

        const bool update_new_max = cmd == CmpControlCmd::UPDATE;
        const bool prop_new_max = cmd == CmpControlCmd::PROP_MAX;
        const bool prop_diff = cmd == CmpControlCmd::PROP_MAX_DIFF;
        const bool prop_zero = cmd == CmpControlCmd::PROP_ZERO;
        const bool do_reset = cmd == CmpControlCmd::RESET;
        const bool prop_exp2_intercepts = cmd == CmpControlCmd::PROP_EXP2_INTERCEPTS;

        const acc_t d_input = (io.in_ctrl.bits.causalCounter==0) ? io.d_input.bits 
                                                                : accMinimum();

        const acc_t downCastDIn = viewEasA(cvtAtoE(d_input));

        if(prop_zero){
            io.d_output.bits = accZero();
        }else if(prop_exp2_intercepts){
            io.d_output.bits = exp2PWLIntercept(current.exp2_counter);
        }else if(update_new_max){
            io.d_output.bits = downCastDIn;
        }else if(prop_new_max){
            io.d_output.bits = -current.newMax;
        }else if(prop_diff){
            io.d_output.bits = accDiff(current.oldMax, current.newMax);
        }else{
            io.d_output.bits = accZero();
        }

        io.d_output.valid = fire && !do_reset;

        io.out_ctrl = io.in_ctrl;

        if(io.in_ctrl.bits.causalCounter == 0){
            io.out_ctrl.bits.causalCounter = 0;
        }else{
            io.out_ctrl.bits.causalCounter = (std::uint8_t)(io.in_ctrl.bits.causalCounter-1);
        }

        if(!fire){
            return;
        }

        if(prop_exp2_intercepts){
            next.exp2_counter = current.exp2_counter+1;
        }

        if(do_reset){
            next.newMax = accMinimum();
            next.oldMax = accMinimum();
        }else if(prop_zero || prop_exp2_intercepts){
            // none
        }else if(update_new_max){
            next.newMax = accMax(d_input, current.newMax);
        }else if(prop_diff){
            next.oldMax = current.newMax;
        }
    }

}  // namespace fsa
