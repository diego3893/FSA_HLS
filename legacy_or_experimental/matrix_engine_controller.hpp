/**
 * @file matrix_engine_controller.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 单FSM、非重叠MatrixEngineController
 * @date 2026-08-25
 * 
 * 
 */
#ifndef MATRIX_ENGINE_CONTROLLER_HPP
#define MATRIX_ENGINE_CONTROLLER_HPP

#include "fsa/execution_plan.hpp"
#include "fsa/instruction.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /// @brief Controller跨logical step保存的最小状态。
    struct MatrixEngineControllerState{
        bool busy = false;
        MatrixInstruction instruction{};
        ap_uint<16> timer = 0;
        ap_uint<16> length = 0;
    };

    struct MatrixEngineControllerInput{
        bool instruction_valid = false;
        MatrixInstruction instruction{};
    };

    struct MatrixEngineControllerOutput{
        bool instruction_ready = false;
        bool instruction_accepted = false;
        bool instruction_done = false;
        bool busy = false;

        /// @brief 当前logical step的完整数据通路控制。
        ExecutionPlanStep plan{};

        /// @brief 对应Chisel MatrixControllerIO.sem_release。
        ValidData<Semaphore> sem_release{};
    };

    void reset_matrix_engine_controller_state(
        MatrixEngineControllerState& state
    );

    /**
     * @brief 推进单FSM Controller一个logical step。
     *
     * 空闲状态接收指令；从下一logical step开始输出timer=0的plan。执行期间
     * instruction_ready保持为false，最后一个plan输出时同时产生done。
     */
    void matrix_engine_controller_step(
        const MatrixEngineControllerState& current,
        MatrixEngineControllerState& next,
        const MatrixEngineControllerInput& input,
        MatrixEngineControllerOutput& output
    );

}  // namespace fsa

#endif  // MATRIX_ENGINE_CONTROLLER_HPP
