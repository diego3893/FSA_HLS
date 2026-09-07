#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    void fsaCoreControllerProcess(
        const unsigned length,
        const bool causal,
        CoreControlStream& control_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                #pragma HLS PIPELINE II=1
                CoreTileControl control{};
                control.meta.initialize = key_tile==0;
                control.meta.finalize = key_tile+1U==tiles;
                control.meta.causal = causal;
                control.meta.query_base =
                    query_tile*(unsigned)SA_COLS;
                control.meta.key_base = key_tile*(unsigned)SA_COLS;

                const unsigned remaining_queries =
                    length-control.meta.query_base.to_uint();
                const unsigned remaining_keys =
                    length-control.meta.key_base.to_uint();
                control.meta.active_queries = (ap_uint<16>)(
                    remaining_queries<(unsigned)SA_COLS
                        ? remaining_queries : (unsigned)SA_COLS
                );
                control.meta.active_keys = (ap_uint<16>)(
                    remaining_keys<(unsigned)SA_COLS
                        ? remaining_keys : (unsigned)SA_COLS
                );

                // LOAD_STATIONARY：Q按Scratchpad读出的自然顺序直通。
                control.load_stationary = InputLayoutControl{};

                // ATTENTION_SCORE：与旧requestInstruction完全相同。
                control.attention_score.rev_input = true;
                control.attention_score.delay_output = true;
                control.attention_score.rev_output = true;

                // ATTENTION_VALUE：V_t使用相同输入反转和阶梯延迟，
                // 但不执行最终输出反转。
                control.attention_value.rev_input = true;
                control.attention_value.delay_output = true;
                control.attention_value.rev_output = false;

                control_stream.write(control);
            }
        }
    }


    SaCycleControl makeSaCycleControl(
        const int cycle,
        const bool initialize
    ){
        #pragma HLS INLINE

        SaCycleControl control{};
        if(cycle<SA_COLS){
            control.load_query = true;
            control.query_index = (PeWaveIndex)cycle;
        }
        if(cycle>=QK_START && cycle<QK_START+SA_COLS){
            control.launch_qk = true;
            control.qk_index = (PeWaveIndex)(cycle-QK_START);
        }

        if(cycle==0 && initialize){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::RESET;
        }else if(cycle>=FIRST_SCORE &&
                cycle<FIRST_SCORE+SA_COLS){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::UPDATE;
            control.cmp_item =
                (PeWaveIndex)(cycle-FIRST_SCORE);
        }else if(cycle==MAX_DIFF_CYCLE){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_MAX_DIFF;
        }else if(cycle==SUB_MAX_CYCLE){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_MAX;
        }else if(cycle>=PWL_START && cycle<=PWL_END){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_EXP2_INTERCEPTS;
            control.cmp_item = (PeWaveIndex)(cycle-PWL_START);
        }else if(cycle==ROW_SUM_CYCLE){
            control.cmp_valid = true;
            control.cmp_op = CmpWaveOp::PROP_ZERO;
        }

        if(cycle==SUB_MAX_CYCLE){
            control.launch_down = true;
            control.down_op = PeWaveOp::SUB_MAX;
        }else if(cycle==SCALE_CYCLE){
            control.launch_down = true;
            control.down_op = PeWaveOp::SCALE;
        }else if(cycle>=PWL_START && cycle<=PWL_END){
            control.launch_down = true;
            control.down_op = PeWaveOp::PWL;
            control.down_item = (PeWaveIndex)(cycle-PWL_START);
        }else if(cycle==ROW_SUM_CYCLE){
            control.launch_down = true;
            control.down_op = PeWaveOp::ROW_SUM;
        }else if(cycle>=PV_START && cycle<PV_START+SA_ROWS){
            control.launch_down = true;
            control.down_op = PeWaveOp::PV;
            control.down_item = (PeWaveIndex)(cycle-PV_START);
        }
        return control;
    }

    /**
     * @brief 逐拍产生SA微程序，不阻塞tile/地址控制的预取路径。
     *
     * 拆成独立DATAFLOW actor后，Scratchpad可以继续提前准备后续tile；
     * 此处仅在SA消费速度不足时通过本控制FIFO自然反压。
     */
    void saExecutionPlanProcess(
        const unsigned length,
        SaCycleControlStream& cycle_control_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                for(int cycle=0; cycle<SA_TILE_CYCLES; ++cycle){
                    #pragma HLS PIPELINE II=1
                    cycle_control_stream.write(
                        makeSaCycleControl(cycle, key_tile==0)
                    );
                }
            }
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
