/**
 * @file cmp_top.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief CMP的Vitis HLS顶层接口
 * @date 2026-08-10
 * 
 * 
 */
#ifndef CMP_TOP_HPP
#define CMP_TOP_HPP

#include "fsa/types.hpp"
#include "fsa/control.hpp"

namespace fsa{
    /**
     * @brief CMP顶层输入
     * 
     */
    struct CMPTopInput{
        /// @brief 复位信号
        bool reset = false;

        /// @brief 输入控制信号
        ValidData<CmpControl> ctrl{};

        /// @brief 输入数据
        ValidData<acc_t> d_input{};
    };

    /**
     * @brief CMP顶层输出
     * 
     */
    struct CMPTopOutput{
        /// @brief 输出信号
        ValidData<CmpControl> ctrl{};

        /// @brief 输出数据
        ValidData<acc_t> d_output{};
    };
}

/**
 * @brief CMP顶层模块
 * 
 * @param input 输入
 * @param output 输出
 */
void cmp_top(const fsa::CMPTopInput& input, fsa::CMPTopOutput& output);

#endif // !CMP_TOP_HPP