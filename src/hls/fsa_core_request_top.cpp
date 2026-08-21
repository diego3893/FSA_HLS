#include "fsa/hls/fsa_core_request_top.hpp"

#include "fsa/execution_plan.hpp"
#include "fsa/fsa_core_datapath.hpp"
#include "fsa/matrix_engine_controller.hpp"

namespace{

    constexpr int Q_BASE_ADDRESS = 0;
    constexpr int K_BASE_ADDRESS = Q_BASE_ADDRESS+fsa::SA_COLS;
    constexpr int VT_BASE_ADDRESS = K_BASE_ADDRESS+fsa::SA_ROWS;
    constexpr int L_ADDRESS = 0;
    constexpr int O_BASE_ADDRESS = 1;

    static_assert(
        VT_BASE_ADDRESS+fsa::SA_ROWS<=fsa::SPAD_ROWS,
        "Q/K/V tile超出Scratchpad容量"
    );
    static_assert(
        O_BASE_ADDRESS+fsa::SA_ROWS<=fsa::ACC_ROWS,
        "L/O布局超出Accumulator RAM容量"
    );
    static_assert(
        fsa::ACC_SUB_BANKS<=fsa::nMemPorts,
        "请求顶层读回一整行时需要每个acc sub-bank一个端口"
    );

    void mapPlanToDatapath(
        const fsa::ExecutionPlanStep& plan,
        fsa::FsaCoreStepInput& step_input
    ){
        #pragma HLS INLINE
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
    }

    void advanceDatapath(
        fsa::FsaCoreDatapathState& state,
        const fsa::FsaCoreStepInput& input,
        fsa::FsaCoreStepOutput& output,
        ap_uint<16>& executed_steps,
        bool& protocol_error
    ){
        #pragma HLS INLINE
        fsa::fsa_core_datapath_step(state, input, output);
        executed_steps = executed_steps+1;

        if(input.sp_read.valid && !output.sp_read_ready){
            protocol_error = true;
        }
        if(input.acc_read.valid && !output.acc_read_ready){
            protocol_error = true;
        }
        if(output.acc_write_valid && !output.acc_write_ready){
            protocol_error = true;
        }
    }

    fsa::MatrixInstruction baseInstruction(const fsa::MxFunc function){
        #pragma HLS INLINE
        fsa::MatrixInstruction instruction{};
        instruction.header.func = function;
        instruction.spad.stride = 1;
        instruction.acc.stride = 1;
        return instruction;
    }

    bool executeInstruction(
        fsa::FsaCoreDatapathState& datapath_state,
        const fsa::MatrixInstruction& instruction,
        ap_uint<16>& executed_steps,
        bool& protocol_error
    ){
        const unsigned step_count =
            fsa::execution_plan_length(instruction);
        fsa::MatrixEngineControllerState controller{};
        fsa::reset_matrix_engine_controller_state(controller);

        fsa::MatrixEngineControllerInput accept{};
        accept.instruction_valid = true;
        accept.instruction = instruction;

        fsa::MatrixEngineControllerState accepted_state{};
        fsa::MatrixEngineControllerOutput accepted_output{};
        fsa::matrix_engine_controller_step(
            controller,
            accepted_state,
            accept,
            accepted_output
        );
        controller = accepted_state;
        if(!accepted_output.instruction_accepted){
            protocol_error = true;
            return false;
        }

        bool done = false;
        for(unsigned timer=0; timer<step_count; ++timer){
            #pragma HLS LOOP_TRIPCOUNT min=5 max=28
            #pragma HLS PIPELINE

            fsa::MatrixEngineControllerState next_controller{};
            fsa::MatrixEngineControllerOutput controller_output{};
            fsa::matrix_engine_controller_step(
                controller,
                next_controller,
                fsa::MatrixEngineControllerInput{},
                controller_output
            );

            if(controller_output.plan.valid){
                fsa::FsaCoreStepInput step_input{};
                mapPlanToDatapath(controller_output.plan, step_input);
                fsa::FsaCoreStepOutput step_output{};
                advanceDatapath(
                    datapath_state,
                    step_input,
                    step_output,
                    executed_steps,
                    protocol_error
                );
            }

            controller = next_controller;
            if(controller_output.instruction_done){
                done = true;
            }
        }

        if(!done){
            protocol_error = true;
        }
        return done;
    }

    void writeSpadRow(
        fsa::FsaCoreDatapathState& state,
        const int address,
        const fsa::elem_t row[fsa::SA_ROWS],
        ap_uint<16>& executed_steps,
        bool& protocol_error
    ){
        constexpr int ELEMENTS_PER_SUB_BANK =
            fsa::SA_ROWS/fsa::SPAD_SUB_BANKS;

        for(int sub_bank=0;
                sub_bank<fsa::SPAD_SUB_BANKS; ++sub_bank){
            fsa::FsaCoreStepInput step_input{};
            step_input.spad_write_valid[0] = true;
            step_input.spad_write_addr[0] = address;
            step_input.spad_write_sub_bank[0] = sub_bank;
            for(int element=0;
                    element<ELEMENTS_PER_SUB_BANK; ++element){
                #pragma HLS UNROLL
                step_input.spad_write_data[0][element] =
                    row[sub_bank*ELEMENTS_PER_SUB_BANK+element];
            }

            fsa::FsaCoreStepOutput step_output{};
            advanceDatapath(
                state,
                step_input,
                step_output,
                executed_steps,
                protocol_error
            );
            if(!step_output.spad_write_ready[0]){
                protocol_error = true;
            }
        }
    }

    void preloadQKV(
        fsa::FsaCoreDatapathState& state,
        const fsa::FsaCoreRequestInput& input,
        ap_uint<16>& executed_steps,
        bool& protocol_error
    ){
        for(int query=0; query<fsa::SA_COLS; ++query){
            fsa::elem_t row[fsa::SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=row type=complete dim=1
            for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                #pragma HLS UNROLL
                row[feature] = input.q[query][feature];
            }
            writeSpadRow(
                state,
                Q_BASE_ADDRESS+query,
                row,
                executed_steps,
                protocol_error
            );
        }

        for(int key=0; key<fsa::SA_ROWS; ++key){
            fsa::elem_t row[fsa::SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=row type=complete dim=1
            for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                #pragma HLS UNROLL
                row[feature] = input.k[key][feature];
            }
            writeSpadRow(
                state,
                K_BASE_ADDRESS+key,
                row,
                executed_steps,
                protocol_error
            );
        }

        // ATTENTION_VALUE读取的是V转置后的Scratchpad布局。
        for(int value_feature=0;
                value_feature<fsa::SA_ROWS; ++value_feature){
            fsa::elem_t row[fsa::SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=row type=complete dim=1
            for(int key=0; key<fsa::SA_ROWS; ++key){
                #pragma HLS UNROLL
                row[key] = input.v[key][value_feature];
            }
            writeSpadRow(
                state,
                VT_BASE_ADDRESS+value_feature,
                row,
                executed_steps,
                protocol_error
            );
        }
    }

    void resetOnlineMax(
        fsa::FsaCoreDatapathState& state,
        ap_uint<16>& executed_steps,
        bool& protocol_error
    ){
        fsa::CmpControl reset{};
        reset.cmd = fsa::CmpControlCmd::RESET;

        // RESET token沿CMP列传播；需要SA_COLS个step覆盖全部CMP。
        for(int cycle=0; cycle<fsa::SA_COLS; ++cycle){
            fsa::FsaCoreStepInput step_input{};
            if(cycle==0){
                step_input.cmp_ctrl = fsa::make_valid(reset);
            }
            fsa::FsaCoreStepOutput step_output{};
            advanceDatapath(
                state,
                step_input,
                step_output,
                executed_steps,
                protocol_error
            );
        }
    }

    void readAccRow(
        fsa::FsaCoreDatapathState& state,
        const int address,
        fsa::acc_t row[fsa::SA_COLS],
        ap_uint<16>& executed_steps,
        bool& protocol_error
    ){
        constexpr int ELEMENTS_PER_SUB_BANK =
            fsa::SA_COLS/fsa::ACC_SUB_BANKS;

        fsa::FsaCoreStepInput request{};
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            #pragma HLS UNROLL
            request.acc_dma_read_valid[sub_bank] = true;
            request.acc_dma_read_addr[sub_bank] = address;
            request.acc_dma_read_sub_bank[sub_bank] = sub_bank;
        }

        fsa::FsaCoreStepOutput request_output{};
        advanceDatapath(
            state,
            request,
            request_output,
            executed_steps,
            protocol_error
        );
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            #pragma HLS UNROLL
            if(!request_output.acc_dma_read_ready[sub_bank]){
                protocol_error = true;
            }
        }

        fsa::FsaCoreStepOutput response{};
        advanceDatapath(
            state,
            fsa::FsaCoreStepInput{},
            response,
            executed_steps,
            protocol_error
        );
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            #pragma HLS UNROLL
            if(!response.acc_dma_response_valid[sub_bank]){
                protocol_error = true;
            }
            for(int element=0;
                    element<ELEMENTS_PER_SUB_BANK; ++element){
                #pragma HLS UNROLL
                row[sub_bank*ELEMENTS_PER_SUB_BANK+element] =
                    response.acc_dma_read_data[sub_bank][element];
            }
        }
    }

}  // namespace

void fsa_core_request_top(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static fsa::FsaCoreDatapathState state{};
    static bool online_sequence_active = false;

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
    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.banks type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.acc_ram.banks type=complete dim=2
    #pragma HLS ARRAY_RESHAPE variable=state.acc_ram.banks type=complete dim=4
    #pragma HLS ARRAY_PARTITION variable=state.acc_dma_response_valid type=complete dim=1

    output = fsa::FsaCoreRequestOutput{};
    output.request_ready = true;

    if(input.reset){
        fsa::reset_fsa_core_datapath_state(state);
        online_sequence_active = false;
        return;
    }
    if(!input.request_valid){
        return;
    }
    if(!input.initialize && !online_sequence_active){
        output.protocol_error = true;
        return;
    }

    if(input.initialize){
        resetOnlineMax(
            state,
            output.executed_steps,
            output.protocol_error
        );
        online_sequence_active = true;
    }

    preloadQKV(
        state,
        input,
        output.executed_steps,
        output.protocol_error
    );

    fsa::MatrixInstruction load =
        baseInstruction(fsa::MxFunc::LOAD_STATIONARY);
    load.spad.addr = Q_BASE_ADDRESS+fsa::SA_COLS-1;
    load.spad.stride = -1;
    executeInstruction(
        state,
        load,
        output.executed_steps,
        output.protocol_error
    );

    fsa::MatrixInstruction score =
        baseInstruction(fsa::MxFunc::ATTENTION_SCORE_COMPUTE);
    score.spad.addr = K_BASE_ADDRESS;
    score.spad.revInput = true;
    score.spad.delayOutput = true;
    score.spad.revOutput = true;
    score.acc.addr = L_ADDRESS;
    score.acc.zero = input.initialize;
    score.acc.causal = input.causal;
    executeInstruction(
        state,
        score,
        output.executed_steps,
        output.protocol_error
    );

    fsa::MatrixInstruction value =
        baseInstruction(fsa::MxFunc::ATTENTION_VALUE_COMPUTE);
    value.spad.addr = VT_BASE_ADDRESS;
    value.spad.revInput = true;
    value.spad.delayOutput = true;
    value.spad.revOutput = false;
    value.acc.addr = O_BASE_ADDRESS;
    value.acc.zero = input.initialize;
    executeInstruction(
        state,
        value,
        output.executed_steps,
        output.protocol_error
    );

    if(input.finalize){
        fsa::MatrixInstruction scale =
            baseInstruction(fsa::MxFunc::ATTENTION_LSE_NORM_SCALE);
        scale.acc.addr = L_ADDRESS;
        executeInstruction(
            state,
            scale,
            output.executed_steps,
            output.protocol_error
        );

        fsa::MatrixInstruction norm =
            baseInstruction(fsa::MxFunc::ATTENTION_LSE_NORM);
        norm.acc.addr = O_BASE_ADDRESS;
        executeInstruction(
            state,
            norm,
            output.executed_steps,
            output.protocol_error
        );
        online_sequence_active = false;
        output.normalized = true;
    }

    fsa::acc_t l_row[fsa::SA_COLS]{};
    #pragma HLS ARRAY_PARTITION variable=l_row type=complete dim=1
    readAccRow(
        state,
        L_ADDRESS,
        l_row,
        output.executed_steps,
        output.protocol_error
    );
    for(int query=0; query<fsa::SA_COLS; ++query){
        #pragma HLS UNROLL
        output.l[query] = l_row[query];
    }

    for(int value_feature=0;
            value_feature<fsa::SA_ROWS; ++value_feature){
        fsa::acc_t o_row[fsa::SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=o_row type=complete dim=1
        readAccRow(
            state,
            O_BASE_ADDRESS+value_feature,
            o_row,
            output.executed_steps,
            output.protocol_error
        );
        for(int query=0; query<fsa::SA_COLS; ++query){
            #pragma HLS UNROLL
            output.o[query][value_feature] = o_row[query];
        }
    }

    output.request_done = !output.protocol_error;
}
