/**
 * @file systolic_array.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明二维SA阵列
 * @date 2026-08-06
 * 
 * 
 */
#ifndef SYSTOLIC_ARRAY_HPP
#define SYSTOLIC_ARRAY_HPP

#include "fsa/control.hpp"
#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief SA的全部端口
     *
     */
    struct SystolicArrayIO{
        /// @brief 从最左侧进入CMP的控制信号
        ValidData<CmpControl> cmp_ctrl;

        /// @brief 从最左侧进入PE的控制信号
        ValidData<PECtrl> pe_ctrl[SA_ROWS];

        /// @brief 左侧进入的数据
        ElemVector pe_data;

        /// @brief 阵列底部的输出
        ValidData<acc_t> acc_out[SA_COLS];
    };

    /**
     * @brief 复位SA中所有PE、CMP
     * 
     * @param state 要复位的SA状态
     */
    void reset_systolic_array_state(SystolicArrayState& state);

    /**
     * @brief 计算整个SA的一个时钟步骤
     * 
     * @param current 本拍开始时状态
     * @param next 本拍结束后状态
     * @param io 本拍输入以及计算得到的输出
     */
    void systolic_array_step(const SystolicArrayState& current,
                            SystolicArrayState& next, SystolicArrayIO& io);

}  // namespace fsa

#endif // !SYSTOLIC_ARRAY_HPP