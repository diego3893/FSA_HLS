/**
 * @file accumulator_top.hpp
 * @brief Accumulator的Vitis HLS顶层接口
 */

#ifndef ACCUMULATOR_TOP_HPP
#define ACCUMULATOR_TOP_HPP

#include "fsa/types.hpp"
#include "fsa/control.hpp"

namespace fsa{

    /**
     * @brief Accumulator顶层输入
     */
    struct AccumulatorTopInput{
        /// @brief 复位顶层内部保存的Accumulator状态
        bool reset = false;

        /// @brief 本逻辑步骤的Accumulator控制命令
        ValidData<AccumulatorControl> ctrl{};

        /// @brief 来自OutputDelayer/SA的输入
        AccVector sa_in{};

        /// @brief 来自Accumulator RAM的输入
        AccVector sram_in{};
    };

}  // namespace fsa

/**
 * @brief Accumulator的HLS顶层函数
 *
 * MOD: 保持ap_ctrl_hs，RECIPROCAL在一次事务内按固定15阶段调度。生成RTL
 * 外再由rtl/accumulator_protocol_wrapper.sv把ap_idle/ap_done映射成明确的
 * busy/result_valid物理时钟信号；实际15拍必须由新综合报告确认。
 */
void accumulator_top(
    const fsa::AccumulatorTopInput& input,
    fsa::AccVector& sram_out,
    bool& sram_write_valid,
    bool& reciprocal_result
);

#endif  // ACCUMULATOR_TOP_HPP
