#include "fsa/hls/accumulator_pipeline_top.hpp"

namespace{

    fsa::AccumulatorPipelineState pipeline_state{};

    void tickPipeline(
        const fsa::AccumulatorToken& input,
        fsa::AccumulatorPipelineOutput& output
    ){
        #pragma HLS INLINE
        fsa::AccumulatorPipelineState next{};
        fsa::accumulator_pipeline_tick(
            pipeline_state,
            next,
            input,
            output
        );
        pipeline_state = next;
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

    for(int cycle=0; cycle<fsa::accumulatorPipelineBatchCycles; ++cycle){
        #pragma HLS PIPELINE II=1
        tickPipeline(input[cycle], output[cycle]);
    }
}
