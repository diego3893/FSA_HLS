/**
 * @file test_delayer.cpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief InputDelayer阶梯延迟的最小逐拍测试
 * @date 2026-08-06
 */

#include <cassert>
#include <cmath>

#include "fsa/delayer.hpp"

namespace{

bool almostEqual(const fsa::elem_t actual, const fsa::elem_t expected){
    return std::fabs(actual-expected)<(fsa::elem_t)1.0e-6F;
}

}  // namespace

int main(){
    // ---------------------------------------------------------------------
    // 测试1：最基本的“第i路延迟i拍”。
    // ---------------------------------------------------------------------
    fsa::ElemInputDelayerState current{};
    fsa::ElemInputDelayerState next{};
    fsa::InputDelayerIO io{};
    fsa::reset_input_delayer_state(current);

    /*
     * 不反转输入和输出，只开启阶梯延迟。连续输入4个向量：
     *
     * cycle0: [ 1,  2,  3,  4]
     * cycle1: [ 5,  6,  7,  8]
     * cycle2: [ 9, 10, 11, 12]
     * cycle3: [13, 14, 15, 16]
     *
     * 第i路延迟i拍，因此cycle3应输出[13,10,7,4]。
     */
    const fsa::elem_t input[4][fsa::SA_ROWS] = {
        {(fsa::elem_t)1,  (fsa::elem_t)2,  (fsa::elem_t)3,  (fsa::elem_t)4},
        {(fsa::elem_t)5,  (fsa::elem_t)6,  (fsa::elem_t)7,  (fsa::elem_t)8},
        {(fsa::elem_t)9,  (fsa::elem_t)10, (fsa::elem_t)11, (fsa::elem_t)12},
        {(fsa::elem_t)13, (fsa::elem_t)14, (fsa::elem_t)15, (fsa::elem_t)16}
    };

    const fsa::elem_t expected[4][fsa::SA_ROWS] = {
        {(fsa::elem_t)1,  (fsa::elem_t)0,  (fsa::elem_t)0, (fsa::elem_t)0},
        {(fsa::elem_t)5,  (fsa::elem_t)2,  (fsa::elem_t)0, (fsa::elem_t)0},
        {(fsa::elem_t)9,  (fsa::elem_t)6,  (fsa::elem_t)3, (fsa::elem_t)0},
        {(fsa::elem_t)13, (fsa::elem_t)10, (fsa::elem_t)7, (fsa::elem_t)4}
    };

    for(int cycle=0; cycle<4; ++cycle){
        fsa::InputDelayerInBits input_bits{};
        input_bits.rev_input = false;
        input_bits.delay_output = true;
        input_bits.rev_output = false;

        for(int row=0; row<fsa::SA_ROWS; ++row){
            input_bits.data[(std::size_t)row] = input[cycle][row];
        }

        io.in = fsa::make_valid(input_bits);
        fsa::input_delayer_step(current, next, io);

        for(int row=0; row<fsa::SA_ROWS; ++row){
            assert(almostEqual(
                io.out[(std::size_t)row],
                expected[cycle][row]));
        }

        current = next;
    }

    // ---------------------------------------------------------------------
    // 测试2：输入反转、输出反转，以及valid=false时的控制保持。
    // ---------------------------------------------------------------------
    fsa::reset_input_delayer_state(current);

    fsa::InputDelayerInBits control_input{};
    control_input.data = {
        (fsa::elem_t)1,
        (fsa::elem_t)2,
        (fsa::elem_t)3,
        (fsa::elem_t)4
    };
    control_input.rev_input = true;
    control_input.delay_output = false;
    control_input.rev_output = false;
    io.in = fsa::make_valid(control_input);
    fsa::input_delayer_step(current, next, io);

    // 只开启rev_input：[1,2,3,4]变为[4,3,2,1]。
    for(int row=0; row<fsa::SA_ROWS; ++row){
        assert(almostEqual(
            io.out[(std::size_t)row],
            (fsa::elem_t)(fsa::SA_ROWS-row)));
    }
    current = next;

    control_input.data = {
        (fsa::elem_t)5,
        (fsa::elem_t)6,
        (fsa::elem_t)7,
        (fsa::elem_t)8
    };
    control_input.rev_input = false;
    control_input.delay_output = false;
    control_input.rev_output = true;
    io.in = fsa::make_valid(control_input);
    fsa::input_delayer_step(current, next, io);

    // 只开启rev_output：[5,6,7,8]变为[8,7,6,5]。
    for(int row=0; row<fsa::SA_ROWS; ++row){
        assert(almostEqual(
            io.out[(std::size_t)row],
            (fsa::elem_t)(8-row)));
    }
    current = next;

    control_input.data = {
        (fsa::elem_t)9,
        (fsa::elem_t)10,
        (fsa::elem_t)11,
        (fsa::elem_t)12
    };
    control_input.rev_input = false;
    control_input.delay_output = true;
    control_input.rev_output = false;
    io.in.bits = control_input;
    io.in.valid = false;
    fsa::input_delayer_step(current, next, io);

    /*
     * 本拍valid=false，所以新的delay_output=true和rev_output=false不能生效。
     * 模块继续使用上一拍保存的delay=false、rev_output=true，结果仍是直接反转。
     */
    for(int row=0; row<fsa::SA_ROWS; ++row){
        assert(almostEqual(
            io.out[(std::size_t)row],
            (fsa::elem_t)(12-row)));
    }

    // ---------------------------------------------------------------------
    // 测试3：OutputDelayer固定执行“输入反转+阶梯延迟+输出反转”。
    // ---------------------------------------------------------------------
    fsa::OutputDelayerState output_current{};
    fsa::OutputDelayerState output_next{};
    fsa::OutputDelayerIO output_io{};
    fsa::reset_output_delayer_state(output_current);

    const fsa::acc_t output_input[4][fsa::SA_COLS] = {
        {(fsa::acc_t)1,  (fsa::acc_t)2,  (fsa::acc_t)3,  (fsa::acc_t)4},
        {(fsa::acc_t)5,  (fsa::acc_t)6,  (fsa::acc_t)7,  (fsa::acc_t)8},
        {(fsa::acc_t)9,  (fsa::acc_t)10, (fsa::acc_t)11, (fsa::acc_t)12},
        {(fsa::acc_t)13, (fsa::acc_t)14, (fsa::acc_t)15, (fsa::acc_t)16}
    };

    const fsa::acc_t output_expected[4][fsa::SA_COLS] = {
        {(fsa::acc_t)0, (fsa::acc_t)0, (fsa::acc_t)0, (fsa::acc_t)4},
        {(fsa::acc_t)0, (fsa::acc_t)0, (fsa::acc_t)3, (fsa::acc_t)8},
        {(fsa::acc_t)0, (fsa::acc_t)2, (fsa::acc_t)7, (fsa::acc_t)12},
        {(fsa::acc_t)1, (fsa::acc_t)6, (fsa::acc_t)11, (fsa::acc_t)16}
    };

    for(int cycle=0; cycle<4; ++cycle){
        for(int col=0; col<fsa::SA_COLS; ++col){
            output_io.in[(std::size_t)col] = output_input[cycle][col];
        }

        fsa::output_delayer_step(output_current, output_next, output_io);

        for(int col=0; col<fsa::SA_COLS; ++col){
            assert(almostEqual(
                output_io.out[(std::size_t)col],
                output_expected[cycle][col]));
        }

        output_current = output_next;
    }

    return 0;
}
