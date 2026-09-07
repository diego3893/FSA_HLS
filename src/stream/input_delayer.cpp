#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    void inputDelayerProcess(
        const unsigned length,
        CoreControlStream& control_in,
        ElemRowStream& q_spad_stream,
        ElemRowStream& k_spad_stream,
        ElemRowStream& v_spad_stream,
        CoreControlStream& control_out,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream
    ){
        #pragma HLS INLINE off

        ElemInputDelayerState delayer_state{};
        #pragma HLS ARRAY_PARTITION \
            variable=delayer_state.out_delay_pipe type=complete dim=0

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const CoreTileControl control = control_in.read();

                ElemRowPacket source[3][SA_COLS]{};
                ElemRowPacket restored[3][SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=source complete dim=0
                #pragma HLS ARRAY_PARTITION variable=restored complete dim=0

                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    source[0][lane] = q_spad_stream.read();
                    source[1][lane] = k_spad_stream.read();
                    source[2][lane] = v_spad_stream.read();
                    restored[0][lane].valid = source[0][lane].valid;
                    restored[1][lane].valid = source[1][lane].valid;
                    restored[2][lane].valid = source[2][lane].valid;
                }

                for(int phase=0; phase<3; ++phase){
                    // LOAD_STATIONARY、SCORE和VALUE时分复用同一套Delayer。
                    reset_input_delayer_state(delayer_state);
                    const InputLayoutControl layout = phase==0
                        ? control.load_stationary
                        : (phase==1
                            ? control.attention_score
                            : control.attention_value);

                    for(int cycle=0;
                            cycle<SA_COLS+SA_ROWS-1; ++cycle){
                        #pragma HLS PIPELINE II=1
                        InputDelayerIO io{};
                        io.in.valid = cycle<SA_COLS;
                        io.in.bits.rev_input = layout.rev_input;
                        io.in.bits.delay_output = layout.delay_output;
                        io.in.bits.rev_output = layout.rev_output;
                        if(cycle<SA_COLS){
                            for(int feature=0;
                                    feature<SA_ROWS; ++feature){
                                #pragma HLS UNROLL
                                io.in.bits.data[(std::size_t)feature] =
                                    source[phase][cycle].data[feature];
                            }
                        }

                        ElemInputDelayerState next_state{};
                        #pragma HLS ARRAY_PARTITION \
                            variable=next_state.out_delay_pipe \
                            type=complete dim=0
                        input_delayer_step(
                            delayer_state, next_state, io
                        );
                        delayer_state = next_state;

                        // 由rev/delay配置反推出当前输出来自哪个tile行和
                        // feature，将真实错拍波前恢复成多周期SA的tile输入。
                        for(int output_lane=0;
                                output_lane<SA_ROWS; ++output_lane){
                            #pragma HLS UNROLL
                            const int internal_lane = layout.rev_output
                                ? SA_ROWS-1-output_lane : output_lane;
                            const int source_feature = layout.rev_input
                                ? SA_ROWS-1-internal_lane : internal_lane;
                            const int source_lane = cycle-
                                (layout.delay_output ? internal_lane : 0);
                            if(source_lane>=0 && source_lane<SA_COLS){
                                restored[phase][source_lane]
                                    .data[source_feature] =
                                    io.out[(std::size_t)output_lane];
                            }
                        }
                    }
                }

                control_out.write(control);
                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    q_sa_stream.write(restored[0][lane]);
                    k_sa_stream.write(restored[1][lane]);
                    v_sa_stream.write(restored[2][lane]);
                }
            }
        }
    }


}  // namespace streaming_v2_detail
}  // namespace fsa
