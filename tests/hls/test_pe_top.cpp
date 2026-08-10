/**
 * @file test_pe_top.cpp
 * @brief 单个 PE HLS 顶层的普通 MAC 测试。
 *
 * 本测试不进入 exp2 路径，只检查：
 * 1. 复位；
 * 2. 从左侧把数据装入 PE.reg；
 * 3. 使用下方输入执行向上的 MAC；
 * 4. 使用上方输入执行向下的 MAC。
 */
#include <cassert>
#include <cmath>
#include <iostream>

#include "fsa/hls/pe_top.hpp"

namespace {

/**
 * @brief 比较两个 acc_t 数值是否近似相等。
 */
bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual - expected) < (fsa::acc_t)1.0e-6F;
}

}  // namespace

int main(){
    fsa::PETopInput input{};
    fsa::PETopOutput output{};

    /* 第一次调用：复位PE内部的reg和exp2Done。 */
    input.reset = true;
    pe_top(input, output);

    assert(!output.ctrl.valid);
    assert(!output.u_output.valid);
    assert(!output.d_output.valid);
    assert(!output.r_output.valid);

    /*
     * 第二次调用：把左侧输入2写入PE.reg。
     * 这次调用结束后，静态状态current.reg应当变为2。
     */
    input = fsa::PETopInput{};
    input.ctrl.valid = true;
    input.ctrl.bits.load_reg_li = true;
    input.l_input.valid = true;
    input.l_input.bits = (fsa::elem_t)2.0F;

    pe_top(input, output);

    assert(output.ctrl.valid);
    assert(output.ctrl.bits.load_reg_li);
    assert(output.r_output.valid);

    /*
     * 第三次调用：执行向上的普通MAC。
     *
     * acc_ui=false，累加输入选择d_input；
     * reg=2，l_input=3，d_input=4；
     * 结果为2*3+4=10，并从u_output输出。
     */
    input = fsa::PETopInput{};
    input.ctrl.valid = true;
    input.ctrl.bits.mac = true;
    input.ctrl.bits.acc_ui = false;
    input.l_input.valid = true;
    input.l_input.bits = (fsa::elem_t)3.0F;
    input.d_input.valid = true;
    input.d_input.bits = (fsa::acc_t)4.0F;

    pe_top(input, output);

    assert(output.u_output.valid);
    assert(almostEqual(output.u_output.bits, (fsa::acc_t)10.0F));
    assert(!output.d_output.valid);

    /*
     * 第四次调用：执行向下的普通MAC。
     *
     * acc_ui=true，累加输入选择u_input；
     * reg仍为2，l_input=5，u_input=1；
     * 结果为2*5+1=11，并从d_output输出。
     */
    input = fsa::PETopInput{};
    input.ctrl.valid = true;
    input.ctrl.bits.mac = true;
    input.ctrl.bits.acc_ui = true;
    input.l_input.valid = true;
    input.l_input.bits = (fsa::elem_t)5.0F;
    input.u_input.valid = true;
    input.u_input.bits = (fsa::acc_t)1.0F;

    pe_top(input, output);

    assert(output.d_output.valid);
    assert(almostEqual(output.d_output.bits, (fsa::acc_t)11.0F));
    assert(!output.u_output.valid);

    std::cout << "[PASS] test_pe_top: normal MAC path" << std::endl;
    return 0;
}
