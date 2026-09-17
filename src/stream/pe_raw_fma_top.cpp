#include "fsa/stream/pe_raw_fma.hpp"

void pe_raw_fma_top(
    const fsa::PeRawFmaInput& input,
    fsa::PeRawFmaOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS PIPELINE II=1
    #pragma HLS LATENCY min=4 max=5

    output = fsa::peRawFma(input);
}
