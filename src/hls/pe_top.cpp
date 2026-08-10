#include "fsa/hls/pe_top.hpp"
#include "fsa/pe.hpp"

void pe_top(const fsa::PETopInput& input, fsa::PETopOutput& output){
    // ap_ctrl_hs控制协议
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    // 压缩为输入输出向量
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static fsa::PEState current{}; // 静态保持，跨函数调用共享

    if(input.reset){
        fsa::reset_pe_state(current);
        output = fsa::PETopOutput{};
        return;
    }

    fsa::PEIO io{};
    io.in_ctrl = input.ctrl;
    io.u_input = input.u_input;
    io.d_input = input.d_input;
    io.l_input = input.l_input;

    fsa::PEState next{};
    fsa::pe_step(current, next, io);

    output.ctrl = io.out_ctrl;
    output.u_output = io.u_output;
    output.d_output = io.d_output;
    output.r_output = io.r_output;

    current = next;

    return;
}
