/**
 * @file accumulator.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明保存scale并更新Accumulator SRAM数据的向量累加器
 * @date 2026-08-06
 * 
 * 
 */
#ifndef ACCUMULATOR_HPP
#define ACCUMULATOR_HPP

#include "fsa/control.hpp"
#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief Acc的全部端口
     *
     */
    struct AccumulatorIO{
        /// @brief 本拍要执行的指令
        ValidData<AccumulatorControl> ctrl_in;

        /// @brief 来自SA的输入
        AccVector sa_in;

        /// @brief 来自Acc SRAM的输入
        AccVector sram_in;

        /// @brief 写回Acc SRAM的输出
        AccVector sram_out;
    };

    /**
     * @brief 复位Acc
     * @param state 要复位的Acc状态
     */
    void reset_accumulator_state(AccumulatorState& state);

    /**
     * @brief Acc的一个时钟步骤
     * @param current 本拍开始时状态
     * @param next 本拍结束后状态
     * @param io 本拍输入以及计算得到的sram_out
     */
    void accumulator_step(const AccumulatorState& current, 
                        AccumulatorState& next, AccumulatorIO& io);

    /**
     * @brief 使用Accumulator的定长恢复除法器计算1/denominator。
     *
     * v2完整事务顶层在最终写回前调用该接口。实现复用旧Accumulator
     * 已有的恢复除法状态机，不在新路径中引入组合FP32除法器。
     */
    acc_t accumulator_reciprocal(acc_t denominator);

}  // namespace fsa

#endif // !ACCUMULATOR_HPP
