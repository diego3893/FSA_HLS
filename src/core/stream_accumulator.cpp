#include "fsa/stream_accumulator.hpp"

#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"

namespace fsa{
    namespace stream_accumulator_detail{

        void stream_accumulator_lane(
            const int query,
            const bool initialize,
            const bool finalize,
            const acc_t max_difference,
            const acc_t rowsum,
            const acc_t pv[SA_ROWS],
            acc_t& online_l,
            acc_t online_o[SA_ROWS],
            acc_t& output_l,
            acc_t output_o[SA_ROWS]
        ){
            #pragma HLS INLINE off
            #pragma HLS FUNCTION_INSTANTIATE variable=query

            const acc_t old_l = initialize ? accZero() : online_l;
            const acc_t alpha = old_l==accZero()
                ? accZero()
                : accExp2PWL(max_difference*attentionScale());
            const acc_t next_l = accUnit(alpha, old_l, rowsum);
            const acc_t inverse_l = finalize && next_l!=accZero()
                ? accumulator_reciprocal(next_l) : accZero();

            online_l = next_l;
            output_l = next_l;
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS PIPELINE II=1
                const acc_t old_o = initialize
                    ? accZero() : online_o[feature];
                const acc_t next_o = accUnit(
                    alpha, old_o, pv[feature]
                );
                online_o[feature] = next_o;
                output_o[feature] = finalize
                    ? (next_l==accZero()
                        ? accZero() : next_o*inverse_l)
                    : next_o;
            }
        }

    }  // namespace stream_accumulator_detail

    void stream_accumulator_update(
        const bool initialize,
        const bool finalize,
        const acc_t max_difference[SA_COLS],
        const acc_t rowsum[SA_COLS],
        const acc_t pv[SA_COLS][SA_ROWS],
        acc_t online_l[SA_COLS],
        acc_t online_o[SA_COLS][SA_ROWS],
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=max_difference type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=rowsum type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=pv type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=online_l type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=online_o type=complete dim=1

        acc_t lane_output_l[SA_COLS]{};
        acc_t lane_output_o[SA_COLS][SA_ROWS]{};
        #pragma HLS ARRAY_PARTITION variable=lane_output_l type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=lane_output_o type=complete dim=0

        #define FSA_STREAM_ACCUMULATOR_LANE(QUERY) \
            stream_accumulator_detail::stream_accumulator_lane( \
                QUERY, initialize, finalize, \
                max_difference[QUERY], rowsum[QUERY], pv[QUERY], \
                online_l[QUERY], online_o[QUERY], \
                lane_output_l[QUERY], lane_output_o[QUERY] \
            )

        // 4x4配置显式产生4个独立Accumulator lane。使用本地完全分割
        // 输出避免聚合output结构体让HLS把四次调用重新串行化。
        #if FSA_SA_COLS==4
        FSA_STREAM_ACCUMULATOR_LANE(0);
        FSA_STREAM_ACCUMULATOR_LANE(1);
        FSA_STREAM_ACCUMULATOR_LANE(2);
        FSA_STREAM_ACCUMULATOR_LANE(3);
        #else
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            FSA_STREAM_ACCUMULATOR_LANE(query);
        }
        #endif
        #undef FSA_STREAM_ACCUMULATOR_LANE

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            output.l[query] = lane_output_l[query];
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS UNROLL
                output.o[query][feature] =
                    lane_output_o[query][feature];
            }
        }
        output.normalized = finalize;
    }

    void stream_fsa_accumulator_process(
        StreamArrayOutputStream& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        static AccumulatorState state{};
        static acc_t local_memory[ACC_ROWS][SA_COLS]{};
        static AccReadRequest pending_read{};
        static acc_t pending_constant = accZero();
        #pragma HLS RESET variable=state
        #pragma HLS RESET variable=local_memory
        #pragma HLS RESET variable=pending_read
        #pragma HLS RESET variable=pending_constant
        #pragma HLS ARRAY_PARTITION variable=state.scale type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=state.reciprocal type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=local_memory type=complete dim=2

        bool done = false;
        while(!done){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT \
                min=1 max=STREAM_MAX_REQUEST_CYCLES
            const StreamArrayOutputToken token = input.read();
            if(token.reset){
                reset_accumulator_state(state);
                pending_read = AccReadRequest{};
                pending_constant = accZero();
                for(int row=0; row<ACC_ROWS; ++row){
                    for(int col=0; col<SA_COLS; ++col){
                        #pragma HLS UNROLL
                        local_memory[row][col] = accZero();
                    }
                }
            }

            AccumulatorIO accumulator_io{};
            #pragma HLS ARRAY_PARTITION variable=accumulator_io.sa_in type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=accumulator_io.sram_in type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=accumulator_io.sram_out type=complete dim=1
            accumulator_io.ctrl_in = token.acc_ctrl;

            unsigned read_address = pending_read.addr.to_uint();
            if(read_address>=(unsigned)ACC_ROWS){
                read_address = 0;
            }
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                accumulator_io.sa_in[(std::size_t)col] =
                    token.array_output[col];
                accumulator_io.sram_in[(std::size_t)col] =
                    pending_read.is_constant
                        ? pending_constant
                        : local_memory[read_address][col];
            }

            AccumulatorState next{};
            accumulator_step(state, next, accumulator_io);
            state = next;

            if(pending_read.valid && pending_read.rmw &&
                    pending_read.addr.to_uint()<(unsigned)ACC_ROWS){
                const unsigned write_address =
                    pending_read.addr.to_uint();
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    local_memory[write_address][col] =
                        accumulator_io.sram_out[(std::size_t)col];
                }
            }

            pending_read = token.acc_read;
            pending_constant = token.acc_constant_value;

            if(token.last && token.request_valid){
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    output.l[query] = local_memory[0][query];
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        output.o[query][feature] =
                            local_memory[1+feature][query];
                    }
                }
                output.normalized = token.finalize;
                output.request_done = true;
            }
            done = token.last;
        }
    }

}  // namespace fsa
