/**
 * @file pe.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明SA中单个PE的输入、输出和逐拍状态转移函数
 * @date 2026-08-06
 * 
 * 
 */
#ifndef PE_HPP
#define PE_HPP

#include "fsa/control.hpp"
#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

/// @brief 单个PE的全部端口
struct PEIO {
    /// @brief 从左侧相邻PE进入的控制信号
    ValidData<PECtrl> in_ctrl;

    /// @brief 传给右侧相邻PE的控制信号
    ValidData<PECtrl> out_ctrl;

    /// @brief 从上方进入的acc_t数据
    ValidData<acc_t> u_input;

    /// @brief 向上方输出的acc_t数据
    ValidData<acc_t> u_output;

    /// @brief 从下方进入的acc_t数据
    ValidData<acc_t> d_input;

    /// @brief 向下方输出的acc_t数据
    ValidData<acc_t> d_output;

    /// @brief 从左侧进入的elem_t数据
    ValidData<elem_t> l_input;

    /// @brief 向右侧输出的elem_t数据
    ValidData<elem_t> r_output;
};

/**
 * @brief PE状态复位
 * @param state 要复位的 PE 状态
 */
void reset_pe_state(PEState& state);

/**
 * @brief 计算PE的一个逻辑步骤
 * @param current 本拍开始状态
 * @param next 下一拍状态
 * @param io 本拍的输入以及本拍计算得到的组合输出
 */
void pe_step(const PEState& current, PEState& next, PEIO& io);

}  // namespace fsa
#endif // !PE_HPP