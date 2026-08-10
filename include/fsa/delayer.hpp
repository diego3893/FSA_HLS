/**
 * @file delayer.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明Delayer的端口与逐拍状态转移函数
 * @date 2026-08-06
 * 
 * 
 */
#ifndef DELAYER_HPP
#define DELAYER_HPP

#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief InputDelayer有效输入中携带的数据与布局控制
     *
     */
    struct InputDelayerInBits{
        /// @brief 一拍输入SA_ROWS个elem_t数据
        ElemVector data{};

        /// @brief true时先把输入向量的顺序反转
        bool rev_input = false;

        /// @brief true时启用第i路延迟i拍的阶梯延迟网络
        bool delay_output = false;

        /// @brief true时在延迟处理后再次反转输出向量
        bool rev_output = false;
    };

    /**
     * @brief InputDelayer的端口
     *
     */
    struct InputDelayerIO{
        /// @brief 本拍输入数据与布局控制
        ValidData<InputDelayerInBits> in;

        /// @brief 输出
        ElemVector out{};
    };

    /**
     * @brief OutputDelayer端口
     *
     */
    struct OutputDelayerIO{
        /// @brief 来自SA各列底部的输出
        AccVector in{};

        /// @brief 重新对齐后送往Acc的输出
        AccVector out{};
    };

    /**
     * @brief 复位InputDelayer状态
     *
     * @param state 要复位的InputDelayer状态
     */
    void reset_input_delayer_state(ElemInputDelayerState& state);

    /**
     * @brief 复位OutputDelayer状态
     *
     * @param state 要复位的OutputDelayer状态
     */
    void reset_output_delayer_state(OutputDelayerState& state);

    /**
     * @brief 计算InputDelayer的一个时钟步骤
     *
     * @param current 本拍开始时状态
     * @param next 下一拍状态
     * @param io 本拍输入以及本函数产生的输出
     */
    void input_delayer_step(const ElemInputDelayerState& current,
            ElemInputDelayerState& next, InputDelayerIO& io);

    /**
     * @brief 计算OutputDelayer的一个时钟步骤
     *
     * @param current 本拍开始时状态
     * @param next 下一拍状态
     * @param io 本拍输入以及本函数产生的输出
     */
    void output_delayer_step(const OutputDelayerState& current,
            OutputDelayerState& next, OutputDelayerIO& io);

}  // namespace fsa

#endif // !DELAYER_HPP