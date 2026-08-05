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
    next = current;

    const CmpControlCmd cmd = io.in_ctrl.bits.cmd;
    const bool fire = io.in_ctrl.valid;

    const bool update_new_max = cmd == CmpControlCmd::UPDATE;
    const bool prop_new_max = cmd == CmpControlCmd::PROP_MAX;
    const bool prop_diff = cmd == CmpControlCmd::PROP_MAX_DIFF;
    const bool prop_zero = cmd == CmpControlCmd::PROP_ZERO;
    const bool do_reset = cmd == CmpControlCmd::RESET;
    const bool prop_exp2_intercepts = cmd == CmpControlCmd::PROP_EXP2_INTERCEPTS;

    const acc_t zero = accZero();

    const acc_t d_input = (io.in_ctrl.bits.causalCounter==0) ? io.d_input.bits 
                                                             : accMinimum();

    const acc_t cmp_in_a = update_new_max ? d_input
                           : (prop_new_max ? zero : current.oldMax);
    const acc_t cmp_in_b = current.newMax;
    const CmpUnitOutput cmpUnit = accCmp(cmp_in_a, cmp_in_b);

    const acc_t downCastDIn = viewEasA(cvtAtoE(d_input));

    if(prop_zero){
        io.d_output.bits = zero;
    }else if(prop_exp2_intercepts){
        io.d_output.bits = exp2PWLIntercept(current.exp2_counter);
    }else if(update_new_max){
        io.d_output.bits = downCastDIn;
    }else{
        io.d_output.bits = cmpUnit.out_diff;
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
        next.exp2_counter = (current.exp2_counter+1) % (exp2_counter_t)exp2PWLPieces;
    }

    if(do_reset){
        next.newMax = accMinimum();
        next.oldMax = accMinimum();
    }else if(prop_zero || prop_exp2_intercepts){
        // none
    }else{
        next.newMax = cmpUnit.out_max;
        if(prop_diff){
            next.oldMax = cmpUnit.out_max;
        }
    }
}

}  // namespace fsa
