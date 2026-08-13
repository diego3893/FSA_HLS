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

    /// @brief FP32 reciprocal恢复除法器的控制阶段
    enum class ReciprocalPhase : std::uint8_t{
        IDLE = 0,
        ITER = 1,
        DONE = 2
    };

    /**
     * @brief 单列FP32 reciprocal恢复除法器的跨拍状态
     *
     * 除法器固定计算1.0/denominator。普通有限数先拆成符号、
     * 无偏指数和24位规格化有效数，再使用恢复除法每拍生成两个商位。
     */
    struct ReciprocalDividerState{
        ReciprocalPhase phase = ReciprocalPhase::IDLE;

        /// @brief 恢复除法余数；24位余数左移一位需要25位
        ap_uint<25> remainder = 0;

        /// @brief 规格化后的24位除数有效数，包含隐藏位
        ap_uint<24> divisor = 0;

        /// @brief 1位整数位、23位小数位、guard和round，共26位
        ap_uint<26> quotient = 0;

        /// @brief 已经执行了多少个两商位ITER周期
        reciprocal_iter_count_t iter_count = 0;

        /// @brief 规格化结果的无偏指数
        ap_int<10> result_exponent = 0;

        /// @brief reciprocal结果符号；1.0为正，因此等于除数符号
        bool result_sign = false;

        /// @brief 输入是否为zero/Inf/NaN等无需普通尾数除法的特殊值
        bool special = false;

        /// @brief 特殊值对应的最终IEEE-754 FP32位模式
        ap_uint<32> special_result_bits = 0;

        /// @brief 除数有效数是否恰好为1.0，即输入是否为2的幂
        bool exact_power_of_two = false;

        /// @brief 最近一次完成并经过RNE舍入的结果
        acc_t result = 0.0F;
    };

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

        /// @brief 每列一套恢复除法器，对应原Scala的每列一个FPAccUnit
        ReciprocalDividerState reciprocal[SA_COLS]{};
    };

}  // namespace fsa
#endif // !STATE_HPP
