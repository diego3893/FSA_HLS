/**
 * @file pe_top.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief PE的Vitis HLS顶层接口
 * @date 2026-08-10
 * 
 * 
 */
#ifndef PE_TOP_HPP
#define PE_TOP_HPP

#include "fsa/types.hpp"
#include "fsa/control.hpp"

namespace fsa{

    /**
     * @brief PE顶层输入
     * 
     */
    struct PETopInput{
        /// @brief 复位信号
        bool reset = false;

        /// @brief 输入控制信号
        ValidData<PECtrl> ctrl{};

        /// @brief 上方输入数据
        ValidData<acc_t> u_input{};

        /// @brief 下方输入数据
        ValidData<acc_t> d_input{};

        /// @brief 左侧输入数据
        ValidData<elem_t> l_input{};
    };

    /**
     * @brief PE顶层输出
     * 
     */
    struct PETopOutput{
        /// @brief 输出控制信号
        ValidData<PECtrl> ctrl{};

        /// @brief 上方输出数据
        ValidData<acc_t> u_output{};

        /// @brief 下方输出数据
        ValidData<acc_t> d_output{};

        /// @brief 右侧输出数据
        ValidData<elem_t> r_output{};
    };
} // namespace fsa

/**
 * @brief PE顶层函数
 * 
 * @param input PE输入
 * @param output PE输出
 */
void pe_top(const fsa::PETopInput& input, fsa::PETopOutput& output);

#endif  // PE_TOP_HPP
