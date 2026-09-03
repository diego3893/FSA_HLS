#include "fsa/stream_delayer.hpp"

#include "fsa/arithmetic.hpp"

namespace fsa{

    void stream_input_delayer(
        const elem_t data[SA_COLS][SA_ROWS],
        const acc_t column_operand[SA_COLS],
        const std::uint16_t active_keys,
        const StreamPeOp op,
        const int wave_count,
        StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1],
        StreamPeTokenStream upward[SA_ROWS+1][SA_COLS],
        StreamPeTokenStream downward[SA_ROWS+1][SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=data type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=column_operand type=complete dim=1

        const bool upward_phase = op==StreamPeOp::QK_MAC;
        for(int wave=0; wave<wave_count; ++wave){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=1 max=STREAM_MAX_PHASE_WAVES

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

                if(op==StreamPeOp::LOAD_Q){
                    token.horizontal = data[wave][row];
                }else if(op==StreamPeOp::QK_MAC){
                    token.valid = wave<(int)active_keys;
                    token.horizontal = token.valid
                        ? data[wave][row] : elemZero();
                }else if(op==StreamPeOp::LOAD_SCORE){
                    token.horizontal = elemZero();
                }else if(op==StreamPeOp::EXP2_PWL){
                    token.horizontal = peExp2Slope(
                        (exp2_counter_t)wave
                    );
                }else if(op==StreamPeOp::SCALE_SCORE){
                    token.horizontal = elemAttentionScale();
                }else if(op==StreamPeOp::ROWSUM_PV){
                    if(wave==0){
                        token.horizontal = elemOne();
                    }else{
                        const bool active_row = row<SA_COLS &&
                            row<(int)active_keys;
                        const int key_row = row<SA_COLS ? row : 0;
                        token.valid = active_row;
                        token.horizontal = active_row
                            ? data[key_row][wave-1] : elemZero();
                    }
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
                if(op==StreamPeOp::LOAD_SCORE){
                    token.vertical = viewEasA(data[col][wave]);
                }else if(op==StreamPeOp::SUB_MAX){
                    token.vertical = column_operand[col];
                }else if(op==StreamPeOp::EXP2_PWL){
                    token.vertical = exp2PWLIntercept(
                        (exp2_counter_t)wave
                    );
                }else{
                    token.vertical = accZero();
                }

                if(upward_phase){
                    upward[SA_ROWS][col].write(token);
                }else{
                    downward[0][col].write(token);
                }
            }
        }
    }

    void stream_output_delayer(
        const StreamPeOp op,
        const int wave_count,
        StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1],
        StreamPeTokenStream upward[SA_ROWS+1][SA_COLS],
        StreamPeTokenStream downward[SA_ROWS+1][SA_COLS],
        StreamPeLaneStream lane[SA_ROWS][SA_COLS],
        acc_t reduction_result[SA_COLS][SA_ROWS],
        elem_t lane_result[SA_ROWS][SA_COLS],
        acc_t scalar_reduction[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=reduction_result type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=lane_result type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=scalar_reduction type=complete dim=1

        const bool upward_phase = op==StreamPeOp::QK_MAC;
        const bool reduction = upward_phase || op==StreamPeOp::ROWSUM_MAC ||
            op==StreamPeOp::PV_MAC || op==StreamPeOp::ROWSUM_PV;
        for(int wave=0; wave<wave_count; ++wave){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=1 max=STREAM_MAX_PHASE_WAVES

            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                const StreamPeToken token = upward_phase
                    ? upward[0][col].read()
                    : downward[SA_ROWS][col].read();
                if(op==StreamPeOp::QK_MAC && wave<SA_COLS){
                    reduction_result[col][wave] = token.vertical;
                }else if(op==StreamPeOp::ROWSUM_PV && wave==0){
                    scalar_reduction[col] = token.vertical;
                }else if(op==StreamPeOp::ROWSUM_PV && wave<=SA_ROWS){
                    reduction_result[col][wave-1] = token.vertical;
                }
            }

            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    const StreamPeLaneResult item = lane[row][col].read();
                    const bool selected = op==StreamPeOp::EXP2_PWL
                        ? item.segment_match : true;
                    if(!reduction && item.valid && selected){
                        lane_result[row][col] = item.element;
                    }
                }
                (void)horizontal[row][SA_COLS].read();
            }
        }
    }

}  // namespace fsa
