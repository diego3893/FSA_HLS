/**
 * @file cmp.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明CMP的输入、输出和逐拍状态转移函数
 * @date 2026-08-06
 * 
 * 
 */
#ifndef CMP_HPP
#define CMP_HPP

#include "fsa/control.hpp"
#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

/**
 * @brief 单个CMP的全部端口
 */
struct CMPIO{
    /// @brief 从本列PE进入的数据
    ValidData<acc_t> d_input;

    /// @brief 向本列PE发送的数据
    ValidData<acc_t> d_output;

    /// @brief 从左侧CMP进入的控制信号
    ValidData<CmpControl> in_ctrl;

    /// @brief 传给右侧CMP的控制信号
    ValidData<CmpControl> out_ctrl;
};

/**
 * @brief CMP复位
 * @param state 要复位的 CMP 状态
 */
void reset_cmp_state(CMPState& state);

/**
 * @brief 计算CMP的一个逻辑步骤
 * @param current 本拍开始状态
 * @param next 下一拍状态
 * @param io 本拍的输入以及本拍计算得到的组合输出
 */
void cmp_step(const CMPState& current, CMPState& next, CMPIO& io);

}  // namespace fsa
#endif // !CMP_HPP