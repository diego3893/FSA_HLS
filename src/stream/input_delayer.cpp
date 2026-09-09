#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    /**
     * Scratchpad每拍送入一整行；本actor把真实Delayer输出逐拍送给SA。
     * 不再先缓存完整tile并在本模块内反解为Q/K/V三份tile。
     */
    void inputDelayerProcess(
        const unsigned length,
        const bool causal,
        CoreControlStream& control_in,
        ElemRowStream& q_spad_stream,
        ElemRowStream& k_spad_stream,
        ElemRowStream& v_spad_stream,
        CoreControlStream& control_out,
        DelayedElemStream& delayed_sa_stream
    ){
        #pragma HLS INLINE off

        ElemInputDelayerState state{};
        #pragma HLS ARRAY_PARTITION variable=state.out_delay_pipe complete dim=0

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const CoreTileControl control = control_in.read();
                control_out.write(control);

                // Q只在每个query tile的第一个KV tile进入Delayer；后续
                // tile直接复用SA边界已经保存的q_tile。
                const int first_phase = key_tile==0 ? 0 : 1;
                for(int phase=first_phase; phase<3; ++phase){
                    const InputLayoutControl layout = phase==0
                        ? control.load_stationary
                        : (phase==1
                            ? control.attention_score
                            : control.attention_value);

                    for(int cycle=0; cycle<SA_COLS+SA_ROWS-1; ++cycle){
                        #pragma HLS PIPELINE II=1
                        ElemRowPacket packet{};
                        if(cycle<SA_COLS){
                            packet = phase==0 ? q_spad_stream.read()
                                : (phase==1 ? k_spad_stream.read()
                                            : v_spad_stream.read());
                        }

                        InputDelayerIO io{};
                        io.in.valid = cycle<SA_COLS;
                        io.in.bits.rev_input = layout.rev_input;
                        io.in.bits.delay_output = layout.delay_output;
                        io.in.bits.rev_output = layout.rev_output;
                        for(int lane=0; lane<SA_ROWS; ++lane){
                            #pragma HLS UNROLL
                            io.in.bits.data[(std::size_t)lane] =
                                packet.valid ? packet.data[lane] : elemZero();
                        }

                        ElemInputDelayerState next{};
                        #pragma HLS ARRAY_PARTITION \
                            variable=next.out_delay_pipe complete dim=0
                        input_delayer_step(state, next, io);
                        state = next;

                        DelayedElemBeat beat{};
                        beat.phase = (DelayerPhase)phase;
                        beat.cycle = (ap_uint<16>)cycle;
                        beat.layout = layout;
                        for(int lane=0; lane<SA_ROWS; ++lane){
                            #pragma HLS UNROLL
                            beat.data[lane] = io.out[(std::size_t)lane];
                        }
                        delayed_sa_stream.write(beat);
                    }
                }
            }
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
