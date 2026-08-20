/**
 * @file accumulator_pipeline_top.hpp
 * @brief 新流水Accumulator的独立Vitis HLS验证顶层
 */

#ifndef ACCUMULATOR_PIPELINE_TOP_HPP
#define ACCUMULATOR_PIPELINE_TOP_HPP

#include "fsa/accumulator_pipeline.hpp"

namespace fsa{

    struct AccumulatorPipelineTopInput{
        bool reset = false;
        AccumulatorToken token{};
    };

    struct AccumulatorPipelineTopOutput{
        AccumulatorPipelineOutput tick{};
    };

    /** @brief 批处理性能验证顶层每次调用推进的逻辑tick数 */
    constexpr int accumulatorPipelineBatchCycles = 128;

}  // namespace fsa

/** @brief 单token事务顶层，用于检查跨调用状态和功能 */
void accumulator_pipeline_top(
    const fsa::AccumulatorPipelineTopInput& input,
    fsa::AccumulatorPipelineTopOutput& output
);

/**
 * @brief 批处理综合顶层
 *
 * 性能验收看该内部循环对连续ACC/ACC_SA能否达到Final II=1，避免把
 * ap_ctrl_hs逐token事务开销误判为快速数据通路的真实Interval。
 */
void accumulator_pipeline_batch_top(
    bool reset,
    const fsa::AccumulatorToken
        input[fsa::accumulatorPipelineBatchCycles],
    fsa::AccumulatorPipelineOutput
        output[fsa::accumulatorPipelineBatchCycles]
);

#endif  // ACCUMULATOR_PIPELINE_TOP_HPP
