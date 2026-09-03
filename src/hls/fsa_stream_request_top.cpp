#include "fsa/hls/fsa_stream_request_top.hpp"

#include "fsa/stream_array.hpp"

void fsa_stream_request_top(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    // FsaCoreRequestInput包含half数组。Vitis不能把带转换运算符的half
    // 递归聚合成一个超宽标量；显式解聚后，各成员仍保持ap_none/ap_vld
    // 接口，并允许内部DATAFLOW对Q/K/V成员独立取数。
    #pragma HLS DISAGGREGATE variable=input
    #pragma HLS DISAGGREGATE variable=output
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_vld port=output

    fsa::fsa_stream_request_run(input, output);
}
