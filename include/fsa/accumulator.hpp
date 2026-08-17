/**
 * @file accumulator.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明保存scale并更新Accumulator SRAM数据的向量累加器
 * @date 2026-08-06
 * 
 * 
 */
#ifndef ACCUMULATOR_HPP
#define ACCUMULATOR_HPP

#include "fsa/control.hpp"
#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief Acc的全部端口
     *
     */
    struct AccumulatorIO{
        /// @brief 本拍要执行的指令
        ValidData<AccumulatorControl> ctrl_in;

        /// @brief 来自SA的输入
        AccVector sa_in;

        /// @brief 来自Acc SRAM的输入
        AccVector sram_in;

        /// @brief 写回Acc SRAM的输出
        AccVector sram_out;

        // MOD: 显式握手/结果状态，替代外部依赖固定调用次数猜测完成时刻。
        /// @brief 本拍可以接收一条新命令；reciprocal运行期间为false
        bool command_ready = true;

        /// @brief sram_out在本拍用于ACC/ACC_SA读改写，可写回Accumulator SRAM
        bool sram_write_valid = false;

        /// @brief reciprocal请求已经接受且尚未完成
        bool reciprocal_busy = false;

        /// @brief reciprocal结果在本拍产生；该信号只保持一个逻辑拍
        bool reciprocal_result_valid = false;
    };

    /**
     * @brief 复位Acc
     * @param state 要复位的Acc状态
     */
    void reset_accumulator_state(AccumulatorState& state);

    /**
     * @brief Acc的一个时钟步骤
     * @param current 本拍开始时状态
     * @param next 本拍结束后状态
     * @param io 本拍输入以及计算得到的sram_out
     */
    void accumulator_step(const AccumulatorState& current, 
                        AccumulatorState& next, AccumulatorIO& io);

    /**
     * @brief 在一次HLS事务内完成四列reciprocal
     *
     * MOD: 该函数把原先需要15次顶层事务推进的FSM收进一个固定15拍事务。
     * 四列状态完全partition，列循环完全unroll；综合后仍须核对实际Latency。
     */
    void accumulator_reciprocal_transaction(
        acc_t scale[SA_COLS],
        AccVector& result
    );

}  // namespace fsa

#endif // !ACCUMULATOR_HPP
