/**
 * @file execution_plan.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明ExecutionPlan结构体和函数
 * @date 2026-08-20
 * 
 * 
 */
#ifndef EXECUTION_PLAN_HPP
#define EXECUTION_PLAN_HPP

#include "fsa/hls/fsa_core_top.hpp"
#include "fsa/instruction.hpp"

namespace fsa{

    /// @brief 一条MatrixInstruction在一个logical step产生的完整控制token
    struct ExecutionPlanStep{
        /// @brief 有效标志
        bool valid = false;
        /// @brief 阶段起始标志
        bool first = false;
        /// @brief 阶段末尾标志
        bool last = false;
        /// @brief 信号量释放标志
        bool semaphore_release = false;
        /// @brief 冲突标志，是否开启第二套FSM
        bool conflict_free = false;

        /// @brief SpSRAM读请求字段
        SpReadRequest sp_read{};
        /// @brief Sp注入的常量
        elem_t sp_constant_value{};
        /// @brief PE控制字段
        ValidData<PECtrl> pe_ctrl[SA_ROWS]{};
        /// @brief CMP控制字段
        ValidData<CmpControl> cmp_ctrl{};

        /// @brief AccSRAM读请求字段
        AccReadRequest acc_read{};
        /// @brief Acc注入的常量
        acc_t acc_constant_value{};
        /// @brief Acc控制字段
        ValidData<AccumulatorControl> acc_ctrl{};
    };

    /**
     * @brief 返回一条指令需要推进的数据通路logical step总数
     * 
     * @param instruction 指令
     * @return unsigned 所需的logical step数量
     */
    unsigned execution_plan_length(const MatrixInstruction& instruction);

    /**
     * @brief 生成指定timer的纯组合控制token
     * 
     * @param instruction 指令
     * @param timer 计时器
     * @return ExecutionPlanStep 生成的控制信号
     */
    ExecutionPlanStep make_execution_plan_step(
        const MatrixInstruction& instruction,
        unsigned timer
    );

}  // namespace fsa

#endif  // EXECUTION_PLAN_HPP
