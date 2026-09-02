#include "fsa/stream_array.hpp"

#include <hls_stream.h>

#include "fsa/arithmetic.hpp"
#include "fsa/stream_accumulator.hpp"
#include "fsa/stream_cmp.hpp"
#include "fsa/stream_delayer.hpp"
#include "fsa/stream_pe.hpp"
#include "fsa/stream_types.hpp"
#include "fsa/systolic_array.hpp"

namespace fsa{
    namespace persistent_array_detail{

        /**
         * @brief Scala SystolicArray的逐拍等价互连。
         *
         * Pipe寄存器形成三条物理路径：左到右、CMP/PE上到下、PE下到上。
         * 每个PE的reg/exp2Done以及每个CMP的oldMax/newMax都只属于该实例。
         */
        void arrayCycle(
            const SystolicArrayState& current,
            SystolicArrayState& next,
            SystolicArrayIO& io
        ){
            #pragma HLS INLINE off
            #pragma HLS ARRAY_PARTITION variable=current.mesh type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=current.cmp_array type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=current.cmp_ctrl_pipe type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=current.pe_ctrl_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=current.cmp_d_output_pipe type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=current.r_output_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=current.d_output_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=current.u_output_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=next.mesh type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=next.cmp_array type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=next.cmp_ctrl_pipe type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=next.pe_ctrl_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=next.cmp_d_output_pipe type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=next.r_output_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=next.d_output_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=next.u_output_pipe type=complete dim=0

            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                io.acc_out[col] =
                    current.d_output_pipe[SA_ROWS-1][col];
            }

            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                CMPIO cmp_io{};
                cmp_io.in_ctrl = col==0
                    ? io.cmp_ctrl
                    : current.cmp_ctrl_pipe[col-1];
                cmp_io.d_input = current.u_output_pipe[0][col];
                stream_cmp_cycle(
                    col, current.cmp_array[col],
                    next.cmp_array[col], cmp_io
                );
                next.cmp_ctrl_pipe[col] = cmp_io.out_ctrl;
                next.cmp_d_output_pipe[col] = cmp_io.d_output;
            }

            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    PEIO pe_io{};
                    pe_io.in_ctrl = col==0
                        ? io.pe_ctrl[row]
                        : current.pe_ctrl_pipe[row][col-1];
                    pe_io.l_input = col==0
                        ? make_valid(io.pe_data[(std::size_t)row])
                        : current.r_output_pipe[row][col-1];
                    pe_io.u_input = row==0
                        ? current.cmp_d_output_pipe[col]
                        : current.d_output_pipe[row-1][col];
                    pe_io.d_input = row==SA_ROWS-1
                        ? make_valid(accZero())
                        : current.u_output_pipe[row+1][col];

                    stream_pe_cycle(
                        row*SA_COLS+col,
                        current.mesh[row][col],
                        next.mesh[row][col], pe_io
                    );
                    next.pe_ctrl_pipe[row][col] = pe_io.out_ctrl;
                    next.r_output_pipe[row][col] = pe_io.r_output;
                    next.d_output_pipe[row][col] = pe_io.d_output;
                    next.u_output_pipe[row][col] = pe_io.u_output;
                }
            }
        }

        void persistentFsaArrayProcess(
            StreamArrayCycleStream& input,
            StreamArrayOutputStream& output
        ){
            #pragma HLS INLINE off
            static SystolicArrayState state{};
            #pragma HLS RESET variable=state
            #pragma HLS ARRAY_PARTITION variable=state.mesh type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=state.cmp_array type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=state.cmp_ctrl_pipe type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=state.pe_ctrl_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=state.cmp_d_output_pipe type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=state.r_output_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=state.d_output_pipe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=state.u_output_pipe type=complete dim=0

            bool done = false;
            while(!done){
                #pragma HLS PIPELINE II=1
                #pragma HLS LOOP_TRIPCOUNT \
                    min=1 max=STREAM_MAX_REQUEST_CYCLES
                const StreamArrayCycleToken token = input.read();
                if(token.reset){
                    reset_systolic_array_state(state);
                }

                SystolicArrayIO io{};
                #pragma HLS ARRAY_PARTITION variable=io.pe_ctrl type=complete dim=1
                #pragma HLS ARRAY_PARTITION variable=io.pe_data type=complete dim=1
                #pragma HLS ARRAY_PARTITION variable=io.acc_out type=complete dim=1
                io.cmp_ctrl = token.cmp_ctrl;
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    io.pe_data[(std::size_t)row] = token.pe_data[row];
                    io.pe_ctrl[(std::size_t)row] = token.pe_ctrl[row];
                }

                SystolicArrayState next{};
                arrayCycle(state, next, io);
                state = next;

                StreamArrayOutputToken result{};
                result.reset = token.reset;
                result.last = token.last;
                result.request_valid = token.request_valid;
                result.finalize = token.finalize;
                result.acc_read = token.acc_read;
                result.acc_constant_value = token.acc_constant_value;
                result.acc_ctrl = token.acc_ctrl;
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    result.array_output[col] =
                        io.acc_out[(std::size_t)col].bits;
                }
                output.write(result);
                done = token.last;
            }
        }

        unsigned logicalStepCount(
            const bool initialize,
            const bool finalize
        ){
            const unsigned cmp_reset = initialize ? SA_COLS : 0;
            const unsigned load_q = SA_COLS+1;
            const unsigned score =
                3*SA_ROWS+SA_COLS+exp2PWLPieces+4;
            const unsigned value = 2*SA_ROWS+SA_COLS;
            const unsigned normalize = finalize
                ? (2+reciprocalLatency)+(SA_ROWS+1)
                : 0;
            return cmp_reset+load_q+score+value+normalize;
        }

    }  // namespace persistent_array_detail

    void stream_fsa_tile(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off

        StreamArrayCycleStream input_to_array("fsa_input_to_array");
        StreamArrayOutputStream array_to_output("fsa_array_to_output");
        StreamArrayOutputStream output_to_acc("fsa_output_to_acc");
        #pragma HLS STREAM variable=input_to_array depth=16
        #pragma HLS STREAM variable=array_to_output depth=16
        #pragma HLS STREAM variable=output_to_acc depth=16
        #pragma HLS DATAFLOW

        stream_fsa_input_delayer_process(input, input_to_array);
        persistent_array_detail::persistentFsaArrayProcess(
            input_to_array, array_to_output
        );
        stream_fsa_output_delayer_process(
            array_to_output, output_to_acc
        );
        stream_fsa_accumulator_process(output_to_acc, output);
    }

    void fsa_stream_request_run(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        static bool online_sequence_active = false;
        #pragma HLS RESET variable=online_sequence_active

        output = FsaCoreRequestOutput{};
        output.request_ready = true;

        if(input.reset){
            online_sequence_active = false;
            if(!input.request_valid){
                stream_fsa_tile(input, output);
                return;
            }
        }
        if(!input.request_valid){
            return;
        }
        if(input.active_keys==0 || input.active_keys>SA_COLS){
            output.protocol_error = true;
            return;
        }
        if(!input.initialize && !online_sequence_active){
            output.protocol_error = true;
            return;
        }
        if(input.initialize){
            online_sequence_active = true;
        }

        stream_fsa_tile(input, output);
        output.executed_steps =
            persistent_array_detail::logicalStepCount(
                input.initialize, input.finalize
            );
        if(!output.protocol_error){
            output.request_done = true;
        }
        online_sequence_active = !input.finalize;
    }

}  // namespace fsa
