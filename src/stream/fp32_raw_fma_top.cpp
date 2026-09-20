#include "fsa/stream/fp32_raw_fma.hpp"

void fp32_raw_fma_top(
    const fsa::Fp32RawFmaInput& input, ap_uint<32>& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_vld port=output
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS PIPELINE II=1
    #pragma HLS LATENCY min=5 max=8
    output = fsa::fp32RawFma(input);
}
