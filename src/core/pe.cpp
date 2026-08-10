#include "fsa/pe.hpp"
#include "fsa/arithmetic.hpp"

namespace fsa{

    void reset_pe_state(PEState& state){
        state.reg = elemZero();
        state.exp2Done = false;
        return;
    }

    void pe_step(const PEState& current, PEState& next, PEIO& io){
        #pragma HLS INLINE
        next = current;
        const PECtrl& ctrl = io.in_ctrl.bits;
        const bool fire = io.in_ctrl.valid;

        const elem_t mac_in_a = current.reg;
        const elem_t mac_in_b = io.l_input.bits;
        const acc_t mac_in_c = ctrl.acc_ui ? io.u_input.bits : io.d_input.bits;

        const PeMacUnitOutput macUnit = peMacUnit(mac_in_a, mac_in_b, mac_in_c, ctrl.exp2);


        io.out_ctrl = io.in_ctrl;

        io.r_output.bits = ctrl.load_reg_li ? current.reg : io.l_input.bits;
        io.r_output.valid = fire && (ctrl.load_reg_li || ctrl.flow_lr);

        io.d_output.bits = (ctrl.mac && ctrl.acc_ui) ? macUnit.out_accType : io.u_input.bits;
        io.d_output.valid = fire && ((ctrl.mac && ctrl.acc_ui) || ctrl.flow_ud);

        io.u_output.bits = (ctrl.mac && !ctrl.acc_ui) ? macUnit.out_accType : io.d_input.bits;
        io.u_output.valid = fire && ((ctrl.mac && !ctrl.acc_ui) || ctrl.flow_du);

        if(!fire){
            return;
        }

        if(ctrl.exp2){
            next.exp2Done = current.exp2Done || macUnit.out_exp2;
        }else{
            next.exp2Done = false;
        }

        if(ctrl.load_reg_li){
            next.reg = io.l_input.bits;
        }else if(ctrl.load_reg_ui){
            next.reg = viewAasE(io.u_input.bits);
        }else if(ctrl.update_reg || (macUnit.out_exp2 && !current.exp2Done)){
            next.reg = macUnit.out_elemType;
        }
    }

}  // namespace fsa