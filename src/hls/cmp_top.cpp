#include "fsa/hls/cmp_top.hpp"
#include "fsa/cmp.hpp"

void cmp_top(const fsa::CMPTopInput& input, fsa::CMPTopOutput& output){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static fsa::CMPState current{};

    if(input.reset){
        fsa::reset_cmp_state(current);
        output = fsa::CMPTopOutput{};
        return;
    }

    fsa::CMPIO io{};
    io.in_ctrl = input.ctrl;
    io.d_input = input.d_input;

    fsa::CMPState next{};
    fsa::cmp_step(current, next, io);

    output.ctrl = io.out_ctrl;
    output.d_output = io.d_output;

    current = next;

    return;
}
