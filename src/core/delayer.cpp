#include "fsa/delayer.hpp"

namespace fsa{
namespace{

/**
 * @brief 复位延迟状态
 *
 * @tparam T 延迟的数据类型
 * @tparam rows 向量长度
 * @param state 要复位的状态
 */
template <typename T, std::size_t rows>
void resetDelayerState(InputDelayerState<T, rows>& state){
    for(std::size_t lane=0; lane<rows; ++lane){
        for(std::size_t stage=0; stage<rows; ++stage){
            state.out_delay_pipe[lane][stage] = T{};
        }
    }
    state.rev_out_r = false;
    state.delay_r = false;
    return;
}

/**
 * @brief 阶梯延迟实现
 *
 * @tparam T 延迟的数据类型
 * @tparam rows 输入和输出向量长度
 * @param current 本拍开始状态
 * @param next 下一拍状态
 * @param input 本拍输入向量
 * @param input_valid 本拍布局控制是否有效
 * @param rev_input 是否先反转输入
 * @param delay_output 是否选择阶梯延迟结果
 * @param rev_output 是否反转最终输出
 * @param output 本拍输出向量
 */
template <typename T, std::size_t rows>
void delayerStep(
    const InputDelayerState<T, rows>& current,
    InputDelayerState<T, rows>& next,
    const FixedVector<T, rows>& input,
    const bool input_valid,
    const bool rev_input,
    const bool delay_output,
    const bool rev_output,
    FixedVector<T, rows>& output){
    next = current;

    T in_data[rows]{};
    T out_delay[rows]{};
    T selected_output[rows]{};

    for(std::size_t lane=0; lane<rows; ++lane){
        const std::size_t input_lane = rev_input ? rows-1-lane : lane;
        in_data[lane] = input[input_lane];
    }

    out_delay[0] = in_data[0];
    for(std::size_t lane=1; lane<rows; ++lane){
        out_delay[lane] = current.out_delay_pipe[lane][lane-1];

        next.out_delay_pipe[lane][0] = in_data[lane];
        for(std::size_t stage=1; stage<lane; ++stage){
            next.out_delay_pipe[lane][stage] = current.out_delay_pipe[lane][stage-1];
        }
    }

    const bool delay = input_valid ? delay_output : current.delay_r;
    const bool rev_out = input_valid ? rev_output : current.rev_out_r;

    if(input_valid){
        next.delay_r = delay_output;
        next.rev_out_r = rev_output;
    }

    for(std::size_t lane=0; lane<rows; ++lane){
        selected_output[lane] = delay ? out_delay[lane] : in_data[lane];
    }

    for(std::size_t lane=0; lane<rows; ++lane){
        const std::size_t output_lane = rev_out ? rows-1-lane : lane;
        output[output_lane] = selected_output[lane];
    }
    return;
}

}  // namespace

void reset_input_delayer_state(ElemInputDelayerState& state){
    resetDelayerState(state);
}

void reset_output_delayer_state(OutputDelayerState& state){
    resetDelayerState(state);
}

void input_delayer_step(const ElemInputDelayerState& current,
            ElemInputDelayerState& next, InputDelayerIO& io){
    delayerStep(
        current,
        next,
        io.in.bits.data,
        io.in.valid,
        io.in.bits.rev_input,
        io.in.bits.delay_output,
        io.in.bits.rev_output,
        io.out);
    return;
}

void output_delayer_step(const OutputDelayerState& current,
            OutputDelayerState& next, OutputDelayerIO& io){
    delayerStep(
        current,
        next,
        io.in,
        true,
        true,
        true,
        true,
        io.out);
        return;
}

}  // namespace fsa
