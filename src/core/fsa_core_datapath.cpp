#include "fsa/fsa_core_datapath.hpp"

namespace fsa{
namespace{

    void fsa_core_datapath_sa_stage(
        const SystolicArrayState& current,
        SystolicArrayState& next,
        SystolicArrayIO& io
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY max=17

        #pragma HLS ARRAY_PARTITION variable=current.mesh complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.cmp_array complete dim=1
        #pragma HLS ARRAY_PARTITION variable=current.cmp_ctrl_pipe complete dim=1
        #pragma HLS ARRAY_PARTITION variable=current.pe_ctrl_pipe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.cmp_d_output_pipe complete dim=1
        #pragma HLS ARRAY_PARTITION variable=current.r_output_pipe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.d_output_pipe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.u_output_pipe complete dim=0

        #pragma HLS ARRAY_PARTITION variable=next.mesh complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.cmp_array complete dim=1
        #pragma HLS ARRAY_PARTITION variable=next.cmp_ctrl_pipe complete dim=1
        #pragma HLS ARRAY_PARTITION variable=next.pe_ctrl_pipe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.cmp_d_output_pipe complete dim=1
        #pragma HLS ARRAY_PARTITION variable=next.r_output_pipe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.d_output_pipe complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.u_output_pipe complete dim=0

        #pragma HLS ARRAY_PARTITION variable=io.pe_ctrl complete dim=1
        #pragma HLS ARRAY_PARTITION variable=io.pe_data complete dim=1
        #pragma HLS ARRAY_PARTITION variable=io.acc_out complete dim=1

        systolic_array_step(current, next, io);
    }

}  // namespace

void reset_fsa_core_datapath_state(FsaCoreDatapathState& state){
    #pragma HLS INLINE

    reset_sp_ram_state(state.sp_ram);
    reset_input_delayer_state(state.input_delayer);
    reset_systolic_array_state(state.sa);
    reset_output_delayer_state(state.output_delayer);
    reset_accumulator_state(state.accumulator);
    reset_acc_ram_state(state.acc_ram);

    state.sp_response_valid = false;
    state.sp_response_is_constant = false;
    state.sp_rev_input = false;
    state.sp_delay_output = false;
    state.sp_rev_output = false;
    state.sp_constant_value = elem_t{};

    state.acc_response_valid = false;
    state.acc_response_is_constant = false;
    state.acc_constant_value = acc_t{};
    state.acc_write_valid = false;
    state.acc_write_addr = 0;

    for(int port=0; port<nMemPorts; ++port){
        #pragma HLS UNROLL
        state.acc_dma_response_valid[port] = false;
    }
}

void fsa_core_datapath_step(
    FsaCoreDatapathState& state,
    const FsaCoreStepInput& input,
    FsaCoreStepOutput& output
){
    #pragma HLS INLINE

    output = FsaCoreStepOutput{};

    // SpRAM整行读
    SpRAMIO sp_ram_io{};
    sp_ram_io.fullRead[0].valid =
        input.sp_read.valid && !input.sp_read.is_constant;
    sp_ram_io.fullRead[0].addr = input.sp_read.addr;
    sp_ram_io.fullRead[0].setFullMask();

    // SpRAM窄写请求
    for(int port=0; port<nMemPorts; ++port){
        #pragma HLS UNROLL
        sp_ram_io.narrowWrite[port].valid = input.spad_write_valid[port];
        sp_ram_io.narrowWrite[port].addr = input.spad_write_addr[port];
        sp_ram_io.narrowWrite[port].subBankIdx =
            input.spad_write_sub_bank[port];
        for(int element=0; element<SA_ROWS/SPAD_SUB_BANKS; ++element){
            #pragma HLS UNROLL
            sp_ram_io.narrowWrite[port].data[(std::size_t)element] =
                input.spad_write_data[port][element];
        }
    }

    // SpRAM运行并握手
    sp_ram_step(state.sp_ram, sp_ram_io);
    output.sp_read_ready = input.sp_read.is_constant
        ? true
        : sp_ram_io.fullRead[0].ready;
    for(int port=0; port<nMemPorts; ++port){
        #pragma HLS UNROLL
        output.spad_write_ready[port] = sp_ram_io.narrowWrite[port].ready;
    }

    // InputDelayer延迟输入
    InputDelayerIO input_delayer_io{};
    input_delayer_io.in.valid = state.sp_response_valid;
    input_delayer_io.in.bits.rev_input = state.sp_rev_input;
    input_delayer_io.in.bits.delay_output = state.sp_delay_output;
    input_delayer_io.in.bits.rev_output = state.sp_rev_output;
    for(int row=0; row<SA_ROWS; ++row){
        #pragma HLS UNROLL
        input_delayer_io.in.bits.data[(std::size_t)row] =
            state.sp_response_is_constant
                ? state.sp_constant_value
                : sp_ram_io.fullRead[0].data[(std::size_t)row];
    }

    ElemInputDelayerState next_input_delayer{};
    #pragma HLS ARRAY_PARTITION variable=next_input_delayer.out_delay_pipe complete dim=0
    input_delayer_step(
        state.input_delayer,
        next_input_delayer,
        input_delayer_io
    );
    output.delayer_out = input_delayer_io.out;

    // SA处理数据
    SystolicArrayIO sa_io{};
    #pragma HLS ARRAY_PARTITION variable=sa_io.pe_ctrl complete dim=1
    #pragma HLS ARRAY_PARTITION variable=sa_io.pe_data complete dim=1
    #pragma HLS ARRAY_PARTITION variable=sa_io.acc_out complete dim=1
    sa_io.pe_data = input_delayer_io.out;
    sa_io.cmp_ctrl = input.cmp_ctrl;
    for(int row=0; row<SA_ROWS; ++row){
        #pragma HLS UNROLL
        sa_io.pe_ctrl[row] = input.pe_ctrl[row];
    }

    SystolicArrayState next_sa{};
    fsa_core_datapath_sa_stage(state.sa, next_sa, sa_io);

    // OutputDelayer对齐数据
    OutputDelayerIO output_delayer_io{};
    for(int col=0; col<SA_COLS; ++col){
        #pragma HLS UNROLL
        output_delayer_io.in[(std::size_t)col] = sa_io.acc_out[col].bits;
    }

    OutputDelayerState next_output_delayer{};
    #pragma HLS ARRAY_PARTITION variable=next_output_delayer.out_delay_pipe complete dim=0
    output_delayer_step(
        state.output_delayer,
        next_output_delayer,
        output_delayer_io
    );
    output.aligned_sa_out = output_delayer_io.out;

    // 数据送入Acc，注意区分输入来源
    AccumulatorIO accumulator_io{};
    #pragma HLS ARRAY_PARTITION variable=accumulator_io.sa_in complete dim=1
    #pragma HLS ARRAY_PARTITION variable=accumulator_io.sram_in complete dim=1
    #pragma HLS ARRAY_PARTITION variable=accumulator_io.sram_out complete dim=1
    accumulator_io.ctrl_in = input.acc_ctrl;
    for(int col=0; col<SA_COLS; ++col){
        #pragma HLS UNROLL
        accumulator_io.sa_in[(std::size_t)col] =
            output_delayer_io.out[(std::size_t)col];
        accumulator_io.sram_in[(std::size_t)col] =
            state.acc_response_is_constant
                ? state.acc_constant_value
                : state.acc_ram.full_read_data[0][(std::size_t)col];
    }

    AccumulatorState next_accumulator{};
    #pragma HLS ARRAY_PARTITION variable=next_accumulator.scale complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next_accumulator.reciprocal complete dim=1
    accumulator_step(
        state.accumulator,
        next_accumulator,
        accumulator_io
    );
    output.accumulator_out = accumulator_io.sram_out;

    // AccRAM整行写
    AccRAMIO acc_ram_io{};
    acc_ram_io.fullRead[0].valid =
        input.acc_read.valid && !input.acc_read.is_constant;
    acc_ram_io.fullRead[0].addr = input.acc_read.addr;
    acc_ram_io.fullRead[0].setFullMask();

    acc_ram_io.fullWrite[0].valid = state.acc_write_valid;
    acc_ram_io.fullWrite[0].addr = state.acc_write_addr;
    acc_ram_io.fullWrite[0].setFullMask();
    for(int col=0; col<SA_COLS; ++col){
        #pragma HLS UNROLL
        acc_ram_io.fullWrite[0].data[(std::size_t)col] =
            accumulator_io.sram_out[(std::size_t)col];
    }

    // AccRAM窄读
    for(int port=0; port<nMemPorts; ++port){
        #pragma HLS UNROLL
        acc_ram_io.narrowRead[port].valid = input.acc_dma_read_valid[port];
        acc_ram_io.narrowRead[port].addr = input.acc_dma_read_addr[port];
        acc_ram_io.narrowRead[port].subBankIdx =
            input.acc_dma_read_sub_bank[port];
    }

    acc_ram_step(state.acc_ram, acc_ram_io);
    output.acc_read_ready = input.acc_read.is_constant
        ? true
        : acc_ram_io.fullRead[0].ready;
    output.acc_write_ready = acc_ram_io.fullWrite[0].ready;
    output.acc_write_valid = state.acc_write_valid;
    output.acc_write_addr = state.acc_write_addr;

    for(int port=0; port<nMemPorts; ++port){
        #pragma HLS UNROLL
        output.acc_dma_read_ready[port] =
            acc_ram_io.narrowRead[port].ready;
        output.acc_dma_response_valid[port] =
            state.acc_dma_response_valid[port];
        for(int element=0; element<SA_COLS/ACC_SUB_BANKS; ++element){
            #pragma HLS UNROLL
            output.acc_dma_read_data[port][element] =
                acc_ram_io.narrowRead[port].data[(std::size_t)element];
        }
    }

    // 保存本step状态
    state.input_delayer = next_input_delayer;
    state.sa = next_sa;
    state.output_delayer = next_output_delayer;
    state.accumulator = next_accumulator;

    const bool sp_request_accepted =
        input.sp_read.valid && output.sp_read_ready;
    const bool acc_request_accepted =
        input.acc_read.valid && output.acc_read_ready;

    // 将本拍的RAM数据在下一拍返回
    state.sp_response_valid = sp_request_accepted;
    state.sp_response_is_constant = input.sp_read.is_constant;
    state.sp_rev_input = input.sp_read.rev_sram_out;
    state.sp_delay_output = input.sp_read.delay_sram_out;
    state.sp_rev_output = input.sp_read.rev_delayer_out;
    state.sp_constant_value = input.sp_constant_value;

    state.acc_response_valid = acc_request_accepted;
    state.acc_response_is_constant = input.acc_read.is_constant;
    state.acc_constant_value = input.acc_constant_value;
    state.acc_write_valid = acc_request_accepted && input.acc_read.rmw;
    state.acc_write_addr = input.acc_read.addr;

    for(int port=0; port<nMemPorts; ++port){
        #pragma HLS UNROLL
        state.acc_dma_response_valid[port] =
            input.acc_dma_read_valid[port] &&
            output.acc_dma_read_ready[port];
    }
}

}  // namespace fsa
