#include "fsa/hls/input_delayer_top.hpp"
#include "fsa/delayer.hpp"

void input_delayer_top(const fsa::InputDelayerTopInput& input, 
        fsa::InputDelayerTopOutput& output){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output
    
    static fsa::ElemInputDelayerState current{};

    if(input.reset){
        fsa::reset_input_delayer_state(current);
        output = fsa::InputDelayerTopOutput{};
        return;
    }

    fsa::InputDelayerIO io{};
    io.in = input.in;

    fsa::ElemInputDelayerState next{};
    fsa::input_delayer_step(current, next, io);

    output.out = io.out;
    current = next;

    return;
}