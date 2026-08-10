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
struct PETopInput{
    bool reset = false;
    ValidData<PECtrl> ctrl{};
    ValidData<acc_t> u_input{};
    ValidData<acc_t> d_input{};
    ValidData<elem_t> l_input{};
};

struct PETopOutput{
    ValidData<PECtrl> ctrl{};
    ValidData<acc_t> u_output{};
    ValidData<acc_t> d_output{};
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
