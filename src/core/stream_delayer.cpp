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
        StreamPeTokenStream vertical[SA_ROWS+1][SA_COLS]
    ){
        #pragma HLS INLINE off

        for(int wave=0; wave<wave_count; ++wave){
            #pragma HLS PIPELINE II=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                StreamPeToken token{};
                token.valid = true;
                token.last = wave+1==wave_count;
                token.op = op;
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
                token.op = op;
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
        elem_t lane_result[SA_ROWS][SA_COLS]
    ){
        #pragma HLS INLINE off

        const bool reduction = op==StreamPeOp::QK_MAC ||
            op==StreamPeOp::ROWSUM_MAC || op==StreamPeOp::PV_MAC;
        for(int wave=0; wave<wave_count; ++wave){
            #pragma HLS PIPELINE II=1
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                const StreamPeToken token =
                    vertical[SA_ROWS][col].read();
                if(reduction && wave<SA_ROWS){
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

}  // namespace fsa
