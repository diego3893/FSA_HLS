/**
 * @file input_delayer_top.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief InputDelayer顶层接口
 * @date 2026-08-11
 * 
 * 
 */
#ifndef INPUT_DELAYER_TOP_HPP
#define INPUT_DELAYER_TOP_HPP

#include "fsa/types.hpp"
#include "fsa/delayer.hpp"

namespace fsa{
    /**
     * @brief InputDelayer顶层输入
     * 
     */
    struct InputDelayerTopInput{
        bool reset = false;
        ValidData<InputDelayerInBits> in{};
    };

    /**
     * @brief InputDelayer顶层输出
     * 
     */
    struct InputDelayerTopOutput{
        ElemVector out{};
    };
}

/**
 * @brief InputDelayer顶层模块
 * 
 * @param input 输入
 * @param output 输出
 */
void input_delayer_top(const fsa::InputDelayerTopInput& input, 
        fsa::InputDelayerTopOutput& output);

#endif // !INPUT_DELAYER_TOP_HPP