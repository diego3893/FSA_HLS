/**
 * @file state.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 定义各模块跨step保存的硬件状态
 * @date 2026-08-05
 * 
 * 
 */
#ifndef STATE_HPP
#define STATE_HPP

#include "fsa/config.hpp"
#include "fsa/control.hpp"
#include "fsa/types.hpp"

namespace fsa{

/**
 * @brief PE内部状态
 * 
 */
struct PEState{
    elem_t reg{};

    /// @brief exp2结果是否已经写入reg
    bool exp2Done = false;
};

/**
 * @brief CMP内部状态
 * 
 */
struct CMPState{
    acc_t oldMax{};
    acc_t newMax{};
    // TODO: rst为-INF，不是0

    /// @brief exp2 PWL截距编号
    exp2_counter_t exp2_counter = 0;
};

/**
 * @brief InputDelayer的跨step状态
 * @tparam T 数据类型
 * @tparam rows 输入向量长度
 * 
 */
template <typename T, std::size_t rows>
struct InputDelayerState{
    /// @brief 输出寄存器
    T out_delay_pipe[rows][rows]{};

    /// @brief rev_output标志
    bool rev_out_r = false;

    /// @brief delay_output标志
    bool delay_r = false;
};

/// @brief elem_t的InputDelayer状态
using ElemInputDelayerState = InputDelayerState<elem_t, SA_ROWS>;

/// @brief acc_t的OutputDelayer状态
using OutputDelayerState = InputDelayerState<acc_t, SA_COLS>;

/**
 * @brief SA内部状态
 * 
 */
struct SystolicArrayState{
    /// @brief PE阵列状态
    PEState mesh[SA_ROWS][SA_COLS]{};

    /// @brief CMP状态
    CMPState cmp_array[SA_COLS]{};

    /**
     * @brief CMP控制信号级间寄存器
     * 
     * CMP[col+1]本拍读取ctrl[col]，out_ctrl写入ctrl[col+1]
     */
    ValidData<CmpControl> cmp_ctrl_pipe[SA_COLS]{};

    /**
     * @brief PE控制信号级间寄存器
     *
     * PE[row][col+1]本拍读取ctrl[row][col]，out_ctrl写入ctrl[row][col+1]
     */
    ValidData<PECtrl> pe_ctrl_pipe[SA_ROWS][SA_COLS]{};

    /// @brief CMP向PE发送的数据Pipe
    ValidData<acc_t> cmp_d_output_pipe[SA_COLS]{};

    /// @brief PE向右的Pipe
    ValidData<elem_t> r_output_pipe[SA_ROWS][SA_COLS]{};

    /// @brief PE向下的Pipe
    ValidData<acc_t> d_output_pipe[SA_ROWS][SA_COLS]{};

    /// @brief PE向上的Pipe
    ValidData<acc_t> u_output_pipe[SA_ROWS][SA_COLS]{};
};

/**
 * @brief Acc内部状态
 * 
 */
struct AccumulatorState{
    acc_t scale[SA_COLS]{};

    /// @brief 是否正在等待多周期reciprocal
    bool reciprocal_busy[SA_COLS]{};

    /// @brief 当前列距离reciprocal结果返回还剩多少拍
    reciprocal_counter_t reciprocal_counter[SA_COLS]{};

    /**
     * @brief reciprocal启动时保存的scale
     *
     * 除法进行期间外部输入可能变化，所以不能在结果返回时重新读取scale
     */
    acc_t reciprocal_operand[SA_COLS]{};
};

}  // namespace fsa
#endif // !STATE_HPP
