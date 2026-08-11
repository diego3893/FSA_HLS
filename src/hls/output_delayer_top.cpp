#include "fsa/hls/output_delayer_top.hpp"
#include "fsa/delayer.hpp"

void output_delayer_top(const fsa::OutputDelayerTopInput& input, fsa::OutputDelayerTopOutput& output){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static fsa::OutputDelayerState current{};

    if(input.reset){
        fsa::reset_output_delayer_state(current);
        output = fsa::OutputDelayerTopOutput{};
        return;
    }

    fsa::OutputDelayerIO io{};
    io.in = input.in;

    fsa::OutputDelayerState next{};
    fsa::output_delayer_step(current, next, io);

    output.out = io.out;
    current = next;

    return;
}