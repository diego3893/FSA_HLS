#include "fsa/hls/fsa_core_request_top.hpp"

#include "fsa/execution_plan.hpp"
#include "fsa/fsa_core_datapath.hpp"

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
        fsa::SA_COLS<=fsa::SA_ROWS,
        "当前矩形请求核要求序列tile不大于head dimension"
    );
    static_assert(
        fsa::SA_COLS<=255,
        "causalCounter当前使用8-bit，SA_COLS不能超过255"
    );
    static_assert(
        fsa::ACC_SUB_BANKS<=fsa::nMemPorts,
        "请求顶层读回一整行时需要每个acc sub-bank一个端口"
    );

    enum class RequestPhase{
        RESET_ONLINE_MAX,
        PRELOAD_Q,
        PRELOAD_K,
        PRELOAD_V,
        EXECUTE_INSTRUCTION,
        READ_L_REQUEST,
        READ_L_RESPONSE,
        READ_O_REQUEST,
        READ_O_RESPONSE,
        DONE
    };

    constexpr unsigned REQUEST_INSTRUCTION_COUNT = 5;
    constexpr unsigned REQUEST_BASE_INSTRUCTION_COUNT = 3;
    constexpr unsigned SPAD_ELEMENTS_PER_SUB_BANK =
        fsa::SA_ROWS/fsa::SPAD_SUB_BANKS;
    constexpr unsigned ACC_ELEMENTS_PER_SUB_BANK =
        fsa::SA_COLS/fsa::ACC_SUB_BANKS;

    // 给矩形阵列和最大head dimension保留宽松但有限的调度上界。
    // 循环不会展开；该常量只帮助HLS分析可变长请求循环。
    constexpr unsigned MAX_REQUEST_SCHEDULER_ITERATIONS =
        fsa::SA_COLS+
        (fsa::SA_COLS+2*fsa::SA_ROWS)*fsa::SPAD_SUB_BANKS+
        16*fsa::SA_ROWS+16*fsa::SA_COLS+
        8*fsa::exp2PWLPieces+fsa::reciprocalLatency+64+
        2*(1+fsa::SA_ROWS);

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

    /**
     * @brief 在调度译码和共享Core之间锁存一拍控制输入
     *
     * 保留独立RTL层次并流水化该拷贝，使phase/instruction译码产生的
     * mux输出先进入寄存器，下一拍才送入advanceDatapath。这个边界用来
     * 切断“调度mux -> advanceDatapath入口”组合路径。
     */
    fsa::FsaCoreStepInput registerDatapathInput(
        const fsa::FsaCoreStepInput& input
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        return input;
    }

    void advanceDatapath(
        fsa::FsaCoreDatapathState& state,
        const fsa::FsaCoreStepInput& input,
        fsa::FsaCoreStepOutput& output,
        ap_uint<16>& executed_steps,
        bool& protocol_error
    ){
        // 本函数在请求调度循环中只有一个静态调用点。所有阶段先生成本拍控制，
        // 再从该调用点推进同一套SA、Accumulator和片上状态通路。
        #pragma HLS INLINE off
        // 最新综合中累加器状态回写依赖使本函数的实际Interval为20。
        // 将II对齐该可实现值；时序切分由上游显式寄存器边界完成。
        #pragma HLS PIPELINE II=20
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

    fsa::MatrixInstruction requestInstruction(
        const fsa::FsaCoreRequestInput& input,
        const unsigned index
    ){
        #pragma HLS INLINE
        switch(index){
        case 0:{
            fsa::MatrixInstruction instruction =
                baseInstruction(fsa::MxFunc::LOAD_STATIONARY);
            instruction.spad.addr = Q_BASE_ADDRESS+fsa::SA_COLS-1;
            instruction.spad.stride = -1;
            return instruction;
        }
        case 1:{
            fsa::MatrixInstruction instruction =
                baseInstruction(fsa::MxFunc::ATTENTION_SCORE_COMPUTE);
            instruction.spad.addr = K_BASE_ADDRESS;
            instruction.spad.revInput = true;
            instruction.spad.delayOutput = true;
            instruction.spad.revOutput = true;
            instruction.acc.addr = L_ADDRESS;
            instruction.acc.zero = input.initialize;
            instruction.acc.causal = input.causal;
            instruction.acc.activeRows = input.active_keys;
            instruction.acc.queryBase = input.query_base;
            instruction.acc.keyBase = input.key_base;
            return instruction;
        }
        case 2:{
            fsa::MatrixInstruction instruction =
                baseInstruction(fsa::MxFunc::ATTENTION_VALUE_COMPUTE);
            instruction.spad.addr = VT_BASE_ADDRESS;
            instruction.spad.revInput = true;
            instruction.spad.delayOutput = true;
            instruction.spad.revOutput = false;
            instruction.acc.addr = O_BASE_ADDRESS;
            instruction.acc.zero = input.initialize;
            return instruction;
        }
        case 3:{
            fsa::MatrixInstruction instruction =
                baseInstruction(fsa::MxFunc::ATTENTION_LSE_NORM_SCALE);
            instruction.acc.addr = L_ADDRESS;
            return instruction;
        }
        default:{
            fsa::MatrixInstruction instruction =
                baseInstruction(fsa::MxFunc::ATTENTION_LSE_NORM);
            instruction.acc.addr = O_BASE_ADDRESS;
            return instruction;
        }
        }
    }

}  // namespace

void fsa::fsa_core_request_run(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
){
    #pragma HLS INLINE off
    #pragma HLS ALLOCATION function instances=advanceDatapath limit=1
    #pragma HLS ALLOCATION function instances=registerDatapathInput limit=1

    static fsa::FsaCoreDatapathState state{};
    static bool online_sequence_active = false;

    #pragma HLS ARRAY_PARTITION variable=state.input_delayer.out_delay_pipe complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.output_delayer.out_delay_pipe complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.mesh complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.cmp_array complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sa.cmp_ctrl_pipe complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sa.pe_ctrl_pipe complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.cmp_d_output_pipe complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.sa.r_output_pipe complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.d_output_pipe complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.sa.u_output_pipe complete dim=0
    #pragma HLS ARRAY_PARTITION variable=state.accumulator.scale complete dim=1
    #pragma HLS ARRAY_PARTITION variable=state.accumulator.reciprocal complete dim=1
    // SRAM banks的分割只在bankedSRAMStep中声明。该函数会内联到当前
    // 顶层；在这里再次对嵌套state成员声明指令会触发2020.2的四维
    // ARRAY_RESHAPE越界问题，并造成同一存储对象存在重复优化指令。
    #pragma HLS ARRAY_PARTITION variable=state.acc_dma_response_valid complete dim=1

    output = fsa::FsaCoreRequestOutput{};
    output.request_ready = true;

    if(input.reset){
        fsa::reset_fsa_core_datapath_state(state);
        online_sequence_active = false;
        if(!input.request_valid){
            return;
        }
    }
    if(!input.request_valid){
        return;
    }
    if(input.active_keys==0 || input.active_keys>fsa::SA_COLS){
        output.protocol_error = true;
        return;
    }
    if(!input.initialize && !online_sequence_active){
        output.protocol_error = true;
        return;
    }

    RequestPhase phase = input.initialize
        ? RequestPhase::RESET_ONLINE_MAX
        : RequestPhase::PRELOAD_Q;
    unsigned phase_index = 0;
    unsigned sub_bank = 0;
    unsigned instruction_index = 0;
    unsigned instruction_timer = 0;
    const unsigned instruction_count = input.finalize
        ? REQUEST_INSTRUCTION_COUNT
        : REQUEST_BASE_INSTRUCTION_COUNT;

    // 所有阶段只生成step_input；完整Core只在循环底部调用一次。上一版外层
    // 目标II=39、实际II=39；本版保持该已收敛目标，仅在Core之前增加
    // 独立的II=1输入寄存器级并取消Core的latency下限。
    for(unsigned scheduler_iteration=0;
            scheduler_iteration<MAX_REQUEST_SCHEDULER_ITERATIONS;
            ++scheduler_iteration){
        #pragma HLS PIPELINE II=39
        #pragma HLS LOOP_TRIPCOUNT min=50 max=20000

        fsa::FsaCoreStepInput step_input{};
        bool issue_step = true;

        switch(phase){
        case RequestPhase::RESET_ONLINE_MAX:{
            if(phase_index==0){
                fsa::CmpControl reset{};
                reset.cmd = fsa::CmpControlCmd::RESET;
                step_input.cmp_ctrl = fsa::make_valid(reset);
            }
            break;
        }

        case RequestPhase::PRELOAD_Q:
        case RequestPhase::PRELOAD_K:
        case RequestPhase::PRELOAD_V:{
            const unsigned address = phase==RequestPhase::PRELOAD_Q
                ? Q_BASE_ADDRESS+phase_index
                : (phase==RequestPhase::PRELOAD_K
                    ? K_BASE_ADDRESS+phase_index
                    : VT_BASE_ADDRESS+phase_index);
            step_input.spad_write_valid[0] = true;
            step_input.spad_write_addr[0] = address;
            step_input.spad_write_sub_bank[0] = sub_bank;

            for(unsigned element=0;
                    element<SPAD_ELEMENTS_PER_SUB_BANK; ++element){
                #pragma HLS UNROLL
                const unsigned feature =
                    sub_bank*SPAD_ELEMENTS_PER_SUB_BANK+element;
                if(phase==RequestPhase::PRELOAD_Q){
                    step_input.spad_write_data[0][element] =
                        input.q[phase_index][feature];
                }else if(phase==RequestPhase::PRELOAD_K){
                    step_input.spad_write_data[0][element] =
                        input.k[phase_index][feature];
                }else{
                    // ATTENTION_VALUE使用按feature行存放的V转置布局。
                    step_input.spad_write_data[0][element] =
                        input.v[feature][phase_index];
                }
            }
            break;
        }

        case RequestPhase::EXECUTE_INSTRUCTION:{
            const fsa::MatrixInstruction instruction =
                requestInstruction(input, instruction_index);
            const fsa::ExecutionPlanStep plan =
                fsa::make_execution_plan_step(
                    instruction,
                    instruction_timer
                );
            issue_step = plan.valid;
            if(plan.valid){
                mapPlanToDatapath(plan, step_input);
            }else{
                output.protocol_error = true;
            }
            break;
        }

        case RequestPhase::READ_L_REQUEST:
        case RequestPhase::READ_O_REQUEST:{
            const unsigned address = phase==RequestPhase::READ_L_REQUEST
                ? L_ADDRESS
                : O_BASE_ADDRESS+phase_index;
            for(int bank=0; bank<fsa::ACC_SUB_BANKS; ++bank){
                #pragma HLS UNROLL
                step_input.acc_dma_read_valid[bank] = true;
                step_input.acc_dma_read_addr[bank] = address;
                step_input.acc_dma_read_sub_bank[bank] = bank;
            }
            break;
        }

        case RequestPhase::READ_L_RESPONSE:
        case RequestPhase::READ_O_RESPONSE:
            // 空step只推进一次状态，使上一拍DMA窄读响应可见。
            break;

        case RequestPhase::DONE:
            issue_step = false;
            break;
        }

        if(!issue_step){
            break;
        }

        // 调度译码和共享Core之间保留一拍寄存器，避免两段
        // 组合逻辑落在同一条时序路径上。
        const fsa::FsaCoreStepInput registered_step_input =
            registerDatapathInput(step_input);

        fsa::FsaCoreStepOutput step_output{};
        advanceDatapath(
            state,
            registered_step_input,
            step_output,
            output.executed_steps,
            output.protocol_error
        );

        switch(phase){
        case RequestPhase::RESET_ONLINE_MAX:
            ++phase_index;
            if(phase_index==(unsigned)fsa::SA_COLS){
                online_sequence_active = true;
                phase = RequestPhase::PRELOAD_Q;
                phase_index = 0;
            }
            break;

        case RequestPhase::PRELOAD_Q:
        case RequestPhase::PRELOAD_K:
        case RequestPhase::PRELOAD_V:{
            if(!step_output.spad_write_ready[0]){
                output.protocol_error = true;
            }
            ++sub_bank;
            if(sub_bank==(unsigned)fsa::SPAD_SUB_BANKS){
                sub_bank = 0;
                ++phase_index;
                const unsigned row_count = phase==RequestPhase::PRELOAD_Q
                    ? fsa::SA_COLS
                    : fsa::SA_ROWS;
                if(phase_index==row_count){
                    if(phase==RequestPhase::PRELOAD_Q){
                        phase = RequestPhase::PRELOAD_K;
                    }else if(phase==RequestPhase::PRELOAD_K){
                        phase = RequestPhase::PRELOAD_V;
                    }else{
                        phase = RequestPhase::EXECUTE_INSTRUCTION;
                        instruction_index = 0;
                        instruction_timer = 0;
                    }
                    phase_index = 0;
                }
            }
            break;
        }

        case RequestPhase::EXECUTE_INSTRUCTION:{
            const fsa::MatrixInstruction instruction =
                requestInstruction(input, instruction_index);
            const unsigned instruction_length =
                fsa::execution_plan_length(instruction);
            ++instruction_timer;
            if(instruction_timer==instruction_length){
                instruction_timer = 0;
                ++instruction_index;
                if(instruction_index==instruction_count){
                    if(input.finalize){
                        online_sequence_active = false;
                        output.normalized = true;
                    }
                    phase = RequestPhase::READ_L_REQUEST;
                }
            }
            break;
        }

        case RequestPhase::READ_L_REQUEST:
        case RequestPhase::READ_O_REQUEST:
            for(int bank=0; bank<fsa::ACC_SUB_BANKS; ++bank){
                #pragma HLS UNROLL
                if(!step_output.acc_dma_read_ready[bank]){
                    output.protocol_error = true;
                }
            }
            phase = phase==RequestPhase::READ_L_REQUEST
                ? RequestPhase::READ_L_RESPONSE
                : RequestPhase::READ_O_RESPONSE;
            break;

        case RequestPhase::READ_L_RESPONSE:
        case RequestPhase::READ_O_RESPONSE:
            for(int bank=0; bank<fsa::ACC_SUB_BANKS; ++bank){
                #pragma HLS UNROLL
                if(!step_output.acc_dma_response_valid[bank]){
                    output.protocol_error = true;
                }
                for(unsigned element=0;
                        element<ACC_ELEMENTS_PER_SUB_BANK; ++element){
                    #pragma HLS UNROLL
                    const unsigned query =
                        bank*ACC_ELEMENTS_PER_SUB_BANK+element;
                    if(phase==RequestPhase::READ_L_RESPONSE){
                        output.l[query] =
                            step_output.acc_dma_read_data[bank][element];
                    }else{
                        output.o[query][phase_index] =
                            step_output.acc_dma_read_data[bank][element];
                    }
                }
            }
            if(phase==RequestPhase::READ_L_RESPONSE){
                phase = RequestPhase::READ_O_REQUEST;
                phase_index = 0;
            }else{
                ++phase_index;
                phase = phase_index==(unsigned)fsa::SA_ROWS
                    ? RequestPhase::DONE
                    : RequestPhase::READ_O_REQUEST;
            }
            break;

        case RequestPhase::DONE:
            break;
        }

        if(phase==RequestPhase::DONE){
            output.request_done = !output.protocol_error;
            break;
        }
    }

    if(phase!=RequestPhase::DONE){
        output.protocol_error = true;
        output.request_done = false;
    }
}

void fsa_core_request_top(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    fsa::fsa_core_request_run(input, output);
}
