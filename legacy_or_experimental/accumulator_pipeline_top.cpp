#include "fsa/hls/accumulator_pipeline_top.hpp"

namespace{

    fsa::AccumulatorPipelineState pipeline_state{};

    void tickPipeline(
        const fsa::AccumulatorToken& input,
        fsa::AccumulatorPipelineOutput& output
    ){
        #pragma HLS INLINE
        fsa::accumulator_pipeline_tick_inplace(
            pipeline_state,
            input,
            output
        );
    }

}  // namespace

void accumulator_pipeline_top(
    const fsa::AccumulatorPipelineTopInput& input,
    fsa::AccumulatorPipelineTopOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    if(input.reset){
        fsa::reset_accumulator_pipeline_state(pipeline_state);
        output = fsa::AccumulatorPipelineTopOutput{};
        return;
    }

    tickPipeline(input.token, output.tick);
}

void accumulator_pipeline_batch_top(
    const bool reset,
    const fsa::AccumulatorToken
        input[fsa::accumulatorPipelineBatchCycles],
    fsa::AccumulatorPipelineOutput
        output[fsa::accumulatorPipelineBatchCycles]
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS INTERFACE ap_memory port=input
    #pragma HLS INTERFACE ap_memory port=output

    if(reset){
        fsa::reset_accumulator_pipeline_state(pipeline_state);
    }

    // EXP_S2接受时写入结果，倒计时18个逻辑tick后才首次读取提交。
    // 明确真实RAW距离，避免HLS把条件慢路径保守视为相邻迭代依赖。
    #pragma HLS DEPENDENCE variable=pipeline_state.exp2_result \
        inter RAW distance=18 true
    for(int cycle=0; cycle<fsa::accumulatorPipelineBatchCycles; ++cycle){
        #pragma HLS PIPELINE II=1
        tickPipeline(input[cycle], output[cycle]);
    }
}
