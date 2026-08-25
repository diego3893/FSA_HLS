#include "fsa/hls/fsa_core_execute_top.hpp"

#include "fsa/execution_plan.hpp"

namespace{

    void mapMaintenancePorts(
        const fsa::FsaCoreExecuteInput& input,
        fsa::FsaCoreStepInput& step_input
    ){
        #pragma HLS INLINE
        for(int port=0; port<fsa::nMemPorts; ++port){
            #pragma HLS UNROLL
            step_input.spad_write_valid[port] =
                input.spad_write_valid[port];
            step_input.spad_write_addr[port] =
                input.spad_write_addr[port];
            step_input.spad_write_sub_bank[port] =
                input.spad_write_sub_bank[port];
            for(int element=0;
                    element<fsa::SA_ROWS/fsa::SPAD_SUB_BANKS; ++element){
                #pragma HLS UNROLL
                step_input.spad_write_data[port][element] =
                    input.spad_write_data[port][element];
            }

            step_input.acc_dma_read_valid[port] =
                input.acc_dma_read_valid[port];
            step_input.acc_dma_read_addr[port] =
                input.acc_dma_read_addr[port];
            step_input.acc_dma_read_sub_bank[port] =
                input.acc_dma_read_sub_bank[port];
        }
    }

    void mapStepOutput(
        const fsa::FsaCoreStepOutput& step_output,
        fsa::FsaCoreExecuteOutput& output
    ){
        #pragma HLS INLINE
        for(int port=0; port<fsa::nMemPorts; ++port){
            #pragma HLS UNROLL
            output.spad_write_ready[port] =
                step_output.spad_write_ready[port];
            output.acc_dma_read_ready[port] =
                step_output.acc_dma_read_ready[port];
            output.acc_dma_response_valid[port] =
                step_output.acc_dma_response_valid[port];
            for(int element=0;
                    element<fsa::SA_COLS/fsa::ACC_SUB_BANKS; ++element){
                #pragma HLS UNROLL
                output.acc_dma_read_data[port][element] =
                    step_output.acc_dma_read_data[port][element];
            }
        }
        output.acc_write_valid = step_output.acc_write_valid;
        output.acc_write_addr = step_output.acc_write_addr;
    }

}  // namespace

void fsa_core_execute_top(
    const fsa::FsaCoreExecuteInput& input,
    fsa::FsaCoreExecuteOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static fsa::FsaCoreDatapathState state{};

    #pragma HLS ARRAY_PARTITION variable=state.input_delayer.out_delay_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.output_delayer.out_delay_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.mesh type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.cmp_array type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sa.cmp_ctrl_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sa.pe_ctrl_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.cmp_d_output_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sa.r_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.d_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.u_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.accumulator.scale type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.accumulator.reciprocal type=complete dim=1

    #pragma HLS ARRAY_PARTITION variable=state.sp_ram.banks type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sp_ram.banks type=complete dim=2
    #pragma HLS ARRAY_RESHAPE variable=state.sp_ram.banks type=complete dim=4
    #pragma HLS ARRAY_PARTITION variable=state.sp_ram.full_read_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sp_ram.full_read_data type=complete dim=2

    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.banks type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.banks type=complete dim=2
    #pragma HLS ARRAY_RESHAPE variable=state.acc_ram.banks type=complete dim=4
    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.full_read_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.full_read_data type=complete dim=2
    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.narrow_read_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.narrow_read_data type=complete dim=2
    #pragma HLS ARRAY_PARTITION variable=state.acc_dma_response_valid type=complete dim=1

    output = fsa::FsaCoreExecuteOutput{};
    if(input.reset){
        fsa::reset_fsa_core_datapath_state(state);
        return;
    }

    if(!input.instruction_valid){
        fsa::FsaCoreStepInput step_input{};
        fsa::FsaCoreStepOutput step_output{};
        mapMaintenancePorts(input, step_input);
        fsa::fsa_core_datapath_step(state, step_input, step_output);
        mapStepOutput(step_output, output);
        return;
    }

    const unsigned step_count =
        fsa::execution_plan_length(input.instruction);
    output.busy = true;

    for(unsigned timer=0; timer<step_count; ++timer){
        #pragma HLS LOOP_TRIPCOUNT min=5 max=28
        #pragma HLS PIPELINE

        const fsa::ExecutionPlanStep plan =
            fsa::make_execution_plan_step(input.instruction, timer);
        fsa::FsaCoreStepInput step_input{};
        step_input.sp_read = plan.sp_read;
        step_input.sp_constant_value = plan.sp_constant_value;
        step_input.cmp_ctrl = plan.cmp_ctrl;
        step_input.acc_read = plan.acc_read;
        step_input.acc_constant_value = plan.acc_constant_value;
        step_input.acc_ctrl = plan.acc_ctrl;
        for(int row=0; row<fsa::SA_ROWS; ++row){
            #pragma HLS UNROLL
            step_input.pe_ctrl[row] = plan.pe_ctrl[row];
        }

        fsa::FsaCoreStepOutput step_output{};
        fsa::fsa_core_datapath_step(state, step_input, step_output);
        mapStepOutput(step_output, output);
    }

    output.busy = false;
    output.instruction_done = step_count!=0;
    output.executed_steps = step_count;
}
