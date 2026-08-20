/**
 * @file accumulator_pipeline.hpp
 * @brief 声明可逐token推进的Accumulator快慢路径流水接口
 */

#ifndef ACCUMULATOR_PIPELINE_HPP
#define ACCUMULATOR_PIPELINE_HPP

#include "fsa/control.hpp"
#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief 快速FMA结果在逻辑tick接口中的固定延迟
     *
     * 该常量统一约束数据、地址、write-enable和tag的移位深度。
     * 最终物理延迟和II仍以独立HLS批处理顶层的综合报告为准。
     */
    constexpr int accumulatorFastLatency = 8;

    /** @brief EXP_S2慢路径提交scale前占用的固定逻辑tick数 */
    constexpr int accumulatorExp2Latency = 18;

    /**
     * @brief Accumulator输入token
     *
     * write_addr、write_enable和tag来自发起Accumulator RAM RMW的控制端，
     * 必须与对应计算数据一起穿过快速流水，不能在结果端重新取当前地址。
     */
    struct AccumulatorToken{
        bool valid = false;
        AccumulatorCmd cmd = AccumulatorCmd::ACC;
        AccVector sa_in{};
        AccVector sram_in{};
        sram_address_t write_addr = 0;
        bool write_enable = false;
        ap_uint<8> tag = 0;
    };

    /** @brief 与快速计算结果严格对齐的写回token */
    struct AccumulatorResultToken{
        bool valid = false;
        AccVector data{};
        sram_address_t write_addr = 0;
        bool write_enable = false;
        ap_uint<8> tag = 0;
    };

    /** @brief 一个快速流水级保存的全部数据和控制元数据 */
    struct AccumulatorFastStage{
        AccumulatorResultToken result{};

        /// @brief true表示该结果提交时还要更新四列scale（EXP_S1）
        bool scale_update = false;
    };

    /// @brief 当前后台慢操作的种类
    enum class AccumulatorSlowOperation : std::uint8_t{
        NONE = 0,
        EXP_S2 = 1,
        RECIPROCAL = 2
    };

    /** @brief 新流水Accumulator跨tick保存的全部硬件状态 */
    struct AccumulatorPipelineState{
        /// @brief 四列完全独立的scale寄存器
        acc_t scale[SA_COLS]{};

        /// @brief valid、结果与RMW元数据使用完全相同深度的快速流水
        AccumulatorFastStage fast_pipe[accumulatorFastLatency]{};

        /// @brief EXP_S1已接受、但结果尚未提交scale
        bool scale_update_pending = false;

        /// @brief 慢速scale变换正在后台运行
        bool scale_busy = false;
        AccumulatorSlowOperation slow_operation =
            AccumulatorSlowOperation::NONE;

        /// @brief EXP_S2的固定提交倒计时和暂存结果
        ap_uint<6> exp2_countdown = 0;
        acc_t exp2_result[SA_COLS]{};

        /// @brief 每列独立的恢复除法状态
        ReciprocalDividerState reciprocal[SA_COLS]{};
    };

    /** @brief 每个逻辑tick产生的握手、状态和结果 */
    struct AccumulatorPipelineOutput{
        /// @brief 本tick是否允许接受input；接受条件为valid&&input_ready
        bool input_ready = false;

        /// @brief 本tick结束后慢路径是否仍忙
        bool scale_busy = false;

        /// @brief 慢路径在本tick完成并提交scale的单拍脉冲
        bool slow_done = false;

        /// @brief 固定延迟快速结果及其原始RMW元数据
        AccumulatorResultToken result{};
    };

    /** @brief 清空scale、流水valid、hazard和慢路径状态 */
    void reset_accumulator_pipeline_state(AccumulatorPipelineState& state);

    /**
     * @brief 推进一次新Accumulator逻辑tick
     *
     * 函数只读取current并只写next。连续ACC/ACC_SA在无scale hazard时
     * 每tick均可接受；scale更新和慢操作按input_ready施加保守背压。
     */
    void accumulator_pipeline_tick(
        const AccumulatorPipelineState& current,
        AccumulatorPipelineState& next,
        const AccumulatorToken& input,
        AccumulatorPipelineOutput& output
    );

}  // namespace fsa

#endif  // ACCUMULATOR_PIPELINE_HPP
