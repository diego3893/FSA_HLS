/**
 * @file control.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief FSA项目控制字段声明
 * @date 2026-08-04
 * 
 * 
 */

#ifndef CONTROL_HPP
#define CONTROL_HPP

#include <array>
#include <cstdint>

namespace fsa{

/**
 * @brief PE控制字段
 * 
 */
struct PECtrl{
    /// @brief MacUnit选通，true时mac结果接入竖直输出
    bool mac = false;

    /// @brief in_c选通，true时取u_input，false取d_input
    bool acc_ui = false;

    /// @brief true则将左侧输入写入reg
    bool load_reg_li = false;

    /// @brief true则将上方输入写入reg
    bool load_reg_ui = false;

    /// @brief true时，从左向右透传
    bool flow_lr = false;

    /// @brief true时，从上向下透传
    bool flow_ud = false;

    /// @brief true时，从下向上透传
    bool flow_du = false;

    /// @brief true时，MacUnit结果存入reg
    bool update_reg = false;

    /// @brief true时，执行exp2；否则mac
    bool exp2 = false;

    /// @brief 获取PE控制字段
    /// @return 9个PE控制信号
    std::array<bool, 9> getCtrlElements() const{
        return {mac, acc_ui, load_reg_li, load_reg_ui,
                 flow_lr, flow_ud, flow_du, update_reg, exp2};
    }
};

/**
 * @brief CMP控制信号
 * 
 */
enum class CmpControlCmd : std::uint8_t{
    /// @brief 使用当前输入更新newMax，并向下输出输入值
    UPDATE = 0,

    /// @brief 向下传播-newMax
    PROP_MAX = 1,

    /// @brief 向下传播oldMax-newMax，更新oldMax=newMax
    PROP_MAX_DIFF = 2,

    /// @brief 向下传播0
    PROP_ZERO = 3,

    /// @brief 重置为-INF
    RESET = 4,

    /// @brief 向下传播PWL截距
    PROP_EXP2_INTERCEPTS = 5
};

/**
 * @brief CMP控制字段
 * 
 */
struct CmpControl {
    /// @brief CMP控制信号
    CmpControlCmd cmd = CmpControlCmd::RESET;

    /// @brief causal mask计数器
    std::uint8_t causalCounter = 0;
};

/**
 * @brief Acc控制信号
 * 
 */
enum class AccumulatorCmd : std::uint8_t{
    /// @brief scale=sa_in*attentionScale
    EXP_S1 = 0,

    /// @brief scale=2^scale
    EXP_S2 = 1,

    /// @brief sram_out=scale*sram_in+sa_in
    ACC_SA = 2,

    /// @brief sram_out=scale*sram_in
    ACC = 3,

    /// @brief scale=sram_in
    SET_SCALE = 4,

    /// @brief scale=1/scale
    RECIPROCAL = 5
};

/**
 * @brief Acc控制字段
 * 
 */
struct AccumulatorControl{
    /// @brief Acc控制信号
    AccumulatorCmd cmd = AccumulatorCmd::SET_SCALE;
};

}  // namespace fsa
#endif // !CONTROL_HPP