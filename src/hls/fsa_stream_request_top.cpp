#include "fsa/hls/fsa_stream_request_top.hpp"

#include "fsa/stream_array.hpp"

void fsa_stream_request_top(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_vld port=output

    fsa::fsa_stream_request_run(input, output);
}
