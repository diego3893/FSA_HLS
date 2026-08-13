#include "fsa/hls/systolic_array_top.hpp"
#include "fsa/systolic_array.hpp"

void systolic_array_top(const fsa::SystolicArrayInput& input,
        fsa::SystolicArrayOutput& output){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    #pragma HLS PIPELINE II=15

    static fsa::SystolicArrayState current{};
    
    #pragma HLS ARRAY_PARTITION variable=current.mesh type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.cmp_array type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.cmp_ctrl_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.pe_ctrl_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.cmp_d_output_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.r_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.d_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.u_output_pipe type=complete dim=0

    if(input.reset){
        fsa::reset_systolic_array_state(current);
        output = fsa::SystolicArrayOutput{};
        return;
    }

    fsa::SystolicArrayIO io{};
    io.cmp_ctrl = input.cmp_ctrl;
    io.pe_data = input.pe_data;

    #pragma HLS ARRAY_PARTITION variable=io.pe_ctrl type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=io.pe_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=io.acc_out type=complete dim=1

    for(int row=0; row<fsa::SA_ROWS; ++row){
        #pragma HLS UNROLL
        io.pe_ctrl[row] = input.pe_ctrl[row];
    }

    fsa::SystolicArrayState next{};
    #pragma HLS ARRAY_PARTITION variable=next.mesh type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=next.cmp_array type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.cmp_ctrl_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.pe_ctrl_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=next.cmp_d_output_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.r_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=next.d_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=next.u_output_pipe type=complete dim=0

    fsa::systolic_array_step(current, next, io);

    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        output.acc_out[col] = io.acc_out[col];
    }

    current = next;

    return;
}
