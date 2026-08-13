/**
 * @file accumulator_top.hpp
 * @brief Accumulator的Vitis HLS顶层接口
 */

#ifndef ACCUMULATOR_TOP_HPP
#define ACCUMULATOR_TOP_HPP

#include "fsa/types.hpp"
#include "fsa/control.hpp"

namespace fsa{

    /**
     * @brief Accumulator顶层输入
     */
    struct AccumulatorTopInput{
        /// @brief 复位顶层内部保存的Accumulator状态
        bool reset = false;

        /// @brief 本逻辑步骤的Accumulator控制命令
        ValidData<AccumulatorControl> ctrl{};

        /// @brief 来自OutputDelayer/SA的输入
        AccVector sa_in{};

        /// @brief 来自Accumulator RAM的输入
        AccVector sram_in{};
    };

    /**
     * @brief Accumulator顶层输出
     */
    struct AccumulatorTopOutput{
        /// @brief 写回Accumulator RAM的数据
        AccVector sram_out{};
    };

}  // namespace fsa

/**
 * @brief Accumulator的HLS顶层函数
 *
 * 一次调用表示Accumulator推进一个逻辑步骤。
 */
void accumulator_top(
    const fsa::AccumulatorTopInput& input,
    fsa::AccumulatorTopOutput& output
);

#endif  // ACCUMULATOR_TOP_HPP