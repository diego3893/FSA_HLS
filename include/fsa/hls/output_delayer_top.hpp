/**
 * @file output_delayer_top.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief OutputDelayer顶层接口
 * @date 2026-08-11
 * 
 * 
 */
#ifndef OUTPUT_DELAYER_TOP_HPP
#define OUTPUT_DELAYER_TOP_HPP

#include "fsa/types.hpp"
#include "fsa/delayer.hpp"

namespace fsa{
    /**
     * @brief OutputDelayer顶层输入
     * 
     */
    struct OutputDelayerTopInput{
        bool reset = false;
        AccVector in{};
    };

    /**
     * @brief OutputDelayer顶层输出
     * 
     */
    struct OutputDelayerTopOutput{
        AccVector out{};
    };
}

/**
 * @brief OutputDelayer顶层模块
 * 
 * @param input 输入
 * @param output 输出
 */
void output_delayer_top(const fsa::OutputDelayerTopInput& input, 
        fsa::OutputDelayerTopOutput& output);

#endif // !OUTPUT_DELAYER_TOP_HPP