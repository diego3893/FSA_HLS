#include "fsa/stream_delayer.hpp"

#include "fsa/arithmetic.hpp"
#include "fsa/delayer.hpp"
#include "fsa/execution_plan.hpp"

namespace fsa{
    namespace persistent_delayer_detail{

        constexpr int Q_BASE_ADDRESS = 0;
        constexpr int K_BASE_ADDRESS = Q_BASE_ADDRESS+SA_COLS;
        constexpr int VT_BASE_ADDRESS = K_BASE_ADDRESS+SA_ROWS;
        constexpr unsigned REQUEST_INSTRUCTION_COUNT = 5;
        constexpr unsigned REQUEST_BASE_INSTRUCTION_COUNT = 3;
        constexpr unsigned MAX_INSTRUCTION_CYCLES =
            4*SA_ROWS+SA_COLS+exp2PWLPieces+reciprocalLatency+16;

        MatrixInstruction baseInstruction(const MxFunc function){
            #pragma HLS INLINE
            MatrixInstruction instruction{};
            instruction.header.func = function;
            instruction.spad.stride = 1;
            instruction.acc.stride = 1;
            return instruction;
        }

        MatrixInstruction requestInstruction(
            const FsaCoreRequestInput& input,
            const unsigned index
        ){
            #pragma HLS INLINE
            if(index==0){
                MatrixInstruction instruction =
                    baseInstruction(MxFunc::LOAD_STATIONARY);
                instruction.spad.addr = Q_BASE_ADDRESS+SA_COLS-1;
                instruction.spad.stride = -1;
                return instruction;
            }
            if(index==1){
                MatrixInstruction instruction =
                    baseInstruction(MxFunc::ATTENTION_SCORE_COMPUTE);
                instruction.spad.addr = K_BASE_ADDRESS;
                instruction.spad.revInput = true;
                instruction.spad.delayOutput = true;
                instruction.spad.revOutput = true;
                instruction.acc.addr = 0;
                instruction.acc.zero = input.initialize;
                instruction.acc.causal = input.causal;
                instruction.acc.activeRows = input.active_keys;
                instruction.acc.queryBase = input.query_base;
                instruction.acc.keyBase = input.key_base;
                return instruction;
            }
            if(index==2){
                MatrixInstruction instruction =
                    baseInstruction(MxFunc::ATTENTION_VALUE_COMPUTE);
                instruction.spad.addr = VT_BASE_ADDRESS;
                instruction.spad.revInput = true;
                instruction.spad.delayOutput = true;
                instruction.spad.revOutput = false;
                instruction.acc.addr = 1;
                instruction.acc.zero = input.initialize;
                return instruction;
            }
            if(index==3){
                MatrixInstruction instruction =
                    baseInstruction(MxFunc::ATTENTION_LSE_NORM_SCALE);
                instruction.acc.addr = 0;
                return instruction;
            }

            MatrixInstruction instruction =
                baseInstruction(MxFunc::ATTENTION_LSE_NORM);
            instruction.acc.addr = 1;
            return instruction;
        }

        void scratchpadResponse(
            const FsaCoreRequestInput& input,
            const SpReadRequest& request,
            const elem_t constant_value,
            ElemVector& data
        ){
            #pragma HLS INLINE
            const unsigned address = request.addr.to_uint();
            for(int lane=0; lane<SA_ROWS; ++lane){
                #pragma HLS UNROLL
                elem_t value = elemZero();
                if(request.is_constant){
                    value = constant_value;
                }else if(address<(unsigned)K_BASE_ADDRESS){
                    value = input.q[address][lane];
                }else if(address<(unsigned)VT_BASE_ADDRESS){
                    value = input.k[address-K_BASE_ADDRESS][lane];
                }else if(address<(unsigned)(VT_BASE_ADDRESS+SA_ROWS)){
                    const unsigned value_feature =
                        address-(unsigned)VT_BASE_ADDRESS;
                    value = input.v[lane][value_feature];
                }
                data[(std::size_t)lane] = value;
            }
        }

        void emitCycle(
            const FsaCoreRequestInput& input,
            const ExecutionPlanStep& plan,
            const bool reset,
            const bool last,
            ElemInputDelayerState& delayer_state,
            SpReadRequest& pending_sp_read,
            elem_t& pending_sp_constant,
            StreamArrayCycleStream& output
        ){
            #pragma HLS INLINE off
            InputDelayerIO delayer_io{};
            delayer_io.in.valid = pending_sp_read.valid;
            delayer_io.in.bits.rev_input =
                pending_sp_read.rev_sram_out;
            delayer_io.in.bits.delay_output =
                pending_sp_read.delay_sram_out;
            delayer_io.in.bits.rev_output =
                pending_sp_read.rev_delayer_out;
            scratchpadResponse(
                input, pending_sp_read, pending_sp_constant,
                delayer_io.in.bits.data
            );

            if(reset){
                reset_input_delayer_state(delayer_state);
            }
            ElemInputDelayerState next_delayer{};
            #pragma HLS ARRAY_PARTITION \
                variable=next_delayer.out_delay_pipe type=complete dim=0
            input_delayer_step(
                delayer_state, next_delayer, delayer_io
            );
            delayer_state = next_delayer;

            StreamArrayCycleToken token{};
            token.reset = reset;
            token.last = last;
            token.request_valid = input.request_valid;
            token.finalize = input.finalize;
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                token.pe_data[row] =
                    delayer_io.out[(std::size_t)row];
                token.pe_ctrl[row] = plan.pe_ctrl[row];
            }
            token.cmp_ctrl = plan.cmp_ctrl;
            token.acc_read = plan.acc_read;
            token.acc_constant_value = plan.acc_constant_value;
            token.acc_ctrl = plan.acc_ctrl;
            output.write(token);

            pending_sp_read = plan.sp_read;
            pending_sp_constant = plan.sp_constant_value;
        }

    }  // namespace persistent_delayer_detail

    void stream_input_delayer(
        const elem_t data[SA_COLS][SA_ROWS],
        const acc_t column_operand[SA_COLS],
        const std::uint16_t active_keys,
        const StreamPeOp op,
        const int wave_count,
        StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1],
        StreamPeTokenStream vertical[SA_ROWS+1][SA_COLS]
    ){
        #pragma HLS INLINE off

        for(int wave=0; wave<wave_count; ++wave){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT \
                min=1 max=STREAM_MAX_PHASE_WAVES
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                StreamPeToken token{};
                token.valid = true;
                token.last = wave+1==wave_count;
                token.op = op==StreamPeOp::ROWSUM_PV
                    ? (wave==0
                        ? StreamPeOp::ROWSUM_MAC
                        : StreamPeOp::PV_MAC)
                    : op;
                token.tag = wave;

                if(op==StreamPeOp::QK_MAC){
                    token.valid = wave<(int)active_keys;
                    token.horizontal = token.valid
                        ? data[wave][row] : elemZero();
                }else if(op==StreamPeOp::PV_MAC){
                    token.valid = row<SA_COLS && row<(int)active_keys;
                    const int key_row = row<SA_COLS ? row : 0;
                    token.horizontal = token.valid
                        ? data[key_row][wave] : elemZero();
                }else if(op==StreamPeOp::ROWSUM_PV){
                    if(wave==0){
                        token.horizontal = elemOne();
                    }else{
                        token.valid = row<SA_COLS &&
                            row<(int)active_keys;
                        const int key_row = row<SA_COLS ? row : 0;
                        token.horizontal = token.valid
                            ? data[key_row][wave-1] : elemZero();
                    }
                }else if(op==StreamPeOp::EXP2_PWL){
                    token.horizontal = peExp2Slope(
                        (exp2_counter_t)wave
                    );
                }else if(op==StreamPeOp::SCALE_SCORE){
                    token.horizontal = elemAttentionScale();
                }else{
                    token.horizontal = elemOne();
                }
                horizontal[row][0].write(token);
            }

            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                StreamPeToken token{};
                token.valid = true;
                token.last = wave+1==wave_count;
                token.op = op==StreamPeOp::ROWSUM_PV
                    ? (wave==0
                        ? StreamPeOp::ROWSUM_MAC
                        : StreamPeOp::PV_MAC)
                    : op;
                token.tag = wave;
                if(op==StreamPeOp::SUB_MAX){
                    token.vertical = column_operand[col];
                }else if(op==StreamPeOp::EXP2_PWL){
                    token.vertical = exp2PWLIntercept(
                        (exp2_counter_t)wave
                    );
                }else{
                    token.vertical = accZero();
                }
                vertical[0][col].write(token);
            }
        }
    }

    void stream_output_delayer(
        const StreamPeOp op,
        const int wave_count,
        StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1],
        StreamPeTokenStream vertical[SA_ROWS+1][SA_COLS],
        StreamPeLaneStream lane[SA_ROWS][SA_COLS],
        acc_t reduction_result[SA_COLS][SA_ROWS],
        elem_t lane_result[SA_ROWS][SA_COLS],
        acc_t scalar_reduction[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION \
            variable=scalar_reduction type=complete dim=1

        const bool reduction = op==StreamPeOp::QK_MAC ||
            op==StreamPeOp::ROWSUM_MAC || op==StreamPeOp::PV_MAC ||
            op==StreamPeOp::ROWSUM_PV;
        for(int wave=0; wave<wave_count; ++wave){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT \
                min=1 max=STREAM_MAX_PHASE_WAVES
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                const StreamPeToken token =
                    vertical[SA_ROWS][col].read();
                if(op==StreamPeOp::ROWSUM_PV && wave==0){
                    scalar_reduction[col] = token.vertical;
                }else if(op==StreamPeOp::ROWSUM_PV &&
                    wave<=SA_ROWS){
                    reduction_result[col][wave-1] = token.vertical;
                }else if(reduction && wave<SA_ROWS){
                    reduction_result[col][wave] = token.vertical;
                }
            }

            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    const StreamPeLaneResult item = lane[row][col].read();
                    const bool selected = op==StreamPeOp::EXP2_PWL
                        ? item.segment_match : true;
                    if(item.valid && selected){
                        lane_result[row][col] = item.element;
                    }
                }
                (void)horizontal[row][SA_COLS].read();
            }
        }
    }

    void stream_fsa_input_delayer_process(
        const FsaCoreRequestInput& input,
        StreamArrayCycleStream& output
    ){
        #pragma HLS INLINE off
        static ElemInputDelayerState delayer_state{};
        #pragma HLS RESET variable=delayer_state
        #pragma HLS ARRAY_PARTITION \
            variable=delayer_state.out_delay_pipe type=complete dim=0

        SpReadRequest pending_sp_read{};
        elem_t pending_sp_constant = elemZero();

        if(input.reset){
            const ExecutionPlanStep empty{};
            persistent_delayer_detail::emitCycle(
                input, empty, true, !input.request_valid,
                delayer_state, pending_sp_read,
                pending_sp_constant, output
            );
            if(!input.request_valid){
                return;
            }
        }

        if(input.initialize){
            for(int cycle=0; cycle<SA_COLS; ++cycle){
                #pragma HLS PIPELINE II=1
                ExecutionPlanStep reset_cmp{};
                if(cycle==0){
                    CmpControl control{};
                    control.cmd = CmpControlCmd::RESET;
                    reset_cmp.cmp_ctrl = make_valid(control);
                }
                persistent_delayer_detail::emitCycle(
                    input, reset_cmp, false, false,
                    delayer_state, pending_sp_read,
                    pending_sp_constant, output
                );
            }
        }

        const unsigned instruction_count = input.finalize
            ? persistent_delayer_detail::REQUEST_INSTRUCTION_COUNT
            : persistent_delayer_detail::REQUEST_BASE_INSTRUCTION_COUNT;
        for(unsigned instruction_index=0;
                instruction_index<
                    persistent_delayer_detail::REQUEST_INSTRUCTION_COUNT;
                ++instruction_index){
            if(instruction_index>=instruction_count){
                break;
            }
            const MatrixInstruction instruction =
                persistent_delayer_detail::requestInstruction(
                    input, instruction_index
                );
            const unsigned length = execution_plan_length(instruction);
            for(unsigned timer=0;
                    timer<persistent_delayer_detail::MAX_INSTRUCTION_CYCLES;
                    ++timer){
                #pragma HLS PIPELINE II=1
                if(timer>=length){
                    break;
                }
                const ExecutionPlanStep plan =
                    make_execution_plan_step(instruction, timer);
                const bool last = instruction_index+1==instruction_count &&
                    timer+1==length;
                persistent_delayer_detail::emitCycle(
                    input, plan, false, last,
                    delayer_state, pending_sp_read,
                    pending_sp_constant, output
                );
            }
        }
    }

    void stream_fsa_output_delayer_process(
        StreamArrayOutputStream& input,
        StreamArrayOutputStream& output
    ){
        #pragma HLS INLINE off
        static OutputDelayerState delayer_state{};
        #pragma HLS RESET variable=delayer_state
        #pragma HLS ARRAY_PARTITION \
            variable=delayer_state.out_delay_pipe type=complete dim=0

        bool done = false;
        while(!done){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT \
                min=1 max=STREAM_MAX_REQUEST_CYCLES
            StreamArrayOutputToken token = input.read();
            if(token.reset){
                reset_output_delayer_state(delayer_state);
            }

            OutputDelayerIO delayer_io{};
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                delayer_io.in[(std::size_t)col] =
                    token.array_output[col];
            }
            OutputDelayerState next_delayer{};
            #pragma HLS ARRAY_PARTITION \
                variable=next_delayer.out_delay_pipe type=complete dim=0
            output_delayer_step(
                delayer_state, next_delayer, delayer_io
            );
            delayer_state = next_delayer;
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                token.array_output[col] =
                    delayer_io.out[(std::size_t)col];
            }
            output.write(token);
            done = token.last;
        }
    }

}  // namespace fsa
