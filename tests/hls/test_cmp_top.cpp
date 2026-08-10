/**
 * @file test_cmp_top.cpp
 * @brief 单个CMP HLS顶层的完整功能测试。
 *
 * 本testbench覆盖：
 * 1. 顶层复位、无效控制和跨事务状态保持；
 * 2. UPDATE、PROP_MAX、PROP_MAX_DIFF、PROP_ZERO和RESET；
 * 3. causalCounter的输入屏蔽、递减和零饱和；
 * 4. UPDATE输出的FP32到FP16转换及按位扩展；
 * 5. 8个PWL编码截距的输出顺序和计数器回绕；
 * 6. RESET命令与顶层复位对exp2_counter的不同行为。
 */
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include <utils/x_hls_utils.h>

#include "fsa/hls/cmp_top.hpp"

namespace {

int failure_count = 0;

/**
 * @brief 检查测试条件，失败时记录项目而不是立即退出。
 * @param condition 应当成立的条件
 * @param message 条件失败时显示的说明
 */
void expect(const bool condition, const std::string& message){
    if(!condition){
        std::cerr << "[FAIL] " << message << std::endl;
        ++failure_count;
    }
}

/** @brief 允许少量FP32舍入误差的数值比较。 */
bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual-expected) <= (fsa::acc_t)1.0e-6F;
}

/** @brief 读取acc_t的IEEE FP32原始位模式。 */
std::uint32_t accBits(const fsa::acc_t value){
    const fp_struct<fsa::acc_t> view(value);
    return (std::uint32_t)view.data().to_uint();
}

/** @brief 读取elem_t的IEEE FP16原始位模式。 */
std::uint16_t elemBits(const fsa::elem_t value){
    const fp_struct<fsa::elem_t> view(value);
    return (std::uint16_t)view.data().to_uint();
}

/** @brief 调用一次CMP顶层，对应一次顶层事务。 */
fsa::CMPTopOutput runCmp(const fsa::CMPTopInput& input){
    fsa::CMPTopOutput output{};
    cmp_top(input, output);
    return output;
}

/**
 * @brief 生成一拍有效的CMP顶层输入。
 * @param cmd 本拍执行的CMP命令
 * @param value 从PE列回到CMP的数据
 * @param causal_counter causal mask计数器
 */
fsa::CMPTopInput makeInput(const fsa::CmpControlCmd cmd,
                           const fsa::acc_t value,
                           const std::uint8_t causal_counter = 0){
    fsa::CMPTopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.cmd = cmd;
    input.ctrl.bits.causalCounter = causal_counter;
    input.d_input.valid = true;
    input.d_input.bits = value;
    return input;
}

/** @brief 执行一条有效CMP命令。 */
fsa::CMPTopOutput runCommand(const fsa::CmpControlCmd cmd,
                             const fsa::acc_t value = (fsa::acc_t)0.0F,
                             const std::uint8_t causal_counter = 0){
    return runCmp(makeInput(cmd, value, causal_counter));
}

/** @brief 执行顶层复位，清除max状态和exp2_counter。 */
void resetCmp(){
    fsa::CMPTopInput input{};
    input.reset = true;

    const fsa::CMPTopOutput output = runCmp(input);
    expect(!output.ctrl.valid, "top reset: ctrl should be invalid");
    expect(!output.d_output.valid,
           "top reset: d_output should be invalid");
}

/**
 * @brief 检查UPDATE输出是否为FP16位模式放在FP32低16位中的结果。
 */
void expectUpdateOutput(const fsa::CMPTopOutput& output,
                        const fsa::acc_t input_value,
                        const std::string& name){
    const std::uint32_t actual_bits = accBits(output.d_output.bits);
    const std::uint16_t expected_elem_bits =
        elemBits((fsa::elem_t)input_value);

    expect(output.d_output.valid, name + ": d_output should be valid");
    expect((actual_bits & 0xffffU)==expected_elem_bits,
           name + ": wrong FP16 bits in d_output[15:0]");
    expect((actual_bits >> 16)==0U,
           name + ": d_output[31:16] should be zero");
}

/** @brief 检查顶层复位和ctrl.valid=false时的状态保持。 */
void testTopResetAndIdle(){
    resetCmp();

    /* 复位后newMax=-INF，所以PROP_MAX输出0-(-INF)=+INF。 */
    fsa::CMPTopOutput output =
        runCommand(fsa::CmpControlCmd::PROP_MAX);
    expect(output.d_output.valid,
           "top reset state: PROP_MAX output should be valid");
    expect(std::isinf(output.d_output.bits) && output.d_output.bits > 0,
           "top reset state: newMax should be -INF");

    resetCmp();
    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)4.0F);

    /* ctrl.valid=false时，数据和控制位都不能改变内部状态。 */
    fsa::CMPTopInput idle{};
    idle.ctrl.valid = false;
    idle.ctrl.bits.cmd = fsa::CmpControlCmd::UPDATE;
    idle.ctrl.bits.causalCounter = 0;
    idle.d_input.valid = true;
    idle.d_input.bits = (fsa::acc_t)9.0F;

    output = runCmp(idle);
    expect(!output.ctrl.valid, "idle: ctrl should be invalid");
    expect(!output.d_output.valid, "idle: d_output should be invalid");

    output = runCommand(fsa::CmpControlCmd::PROP_MAX);
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)-4.0F),
           "idle: invalid UPDATE changed newMax");
}

/** @brief 检查UPDATE的输出转换、最大值更新和状态保存。 */
void testUpdateAndPropMax(){
    resetCmp();

    fsa::CMPTopOutput output =
        runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)3.0F);
    expect(output.ctrl.valid, "UPDATE: ctrl should be valid");
    expect(output.ctrl.bits.cmd==fsa::CmpControlCmd::UPDATE,
           "UPDATE: command was not propagated");
    expectUpdateOutput(output, (fsa::acc_t)3.0F, "UPDATE 3");

    output = runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)5.0F);
    expectUpdateOutput(output, (fsa::acc_t)5.0F, "UPDATE 5");

    /* 输入4仍应向下输出，但newMax应继续保持5。 */
    output = runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)4.0F);
    expectUpdateOutput(output, (fsa::acc_t)4.0F, "UPDATE 4");

    output = runCommand(fsa::CmpControlCmd::PROP_MAX);
    expect(output.d_output.valid, "PROP_MAX: output should be valid");
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)-5.0F),
           "PROP_MAX: expected 0-newMax=-5");

    /* 使用不能由FP16精确表示的输入，检查UPDATE的舍入和按位扩展。 */
    resetCmp();
    output = runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)1.1F);
    expect(output.d_output.valid,
           "UPDATE FP16 rounding: d_output should be valid");
    expect(accBits(output.d_output.bits)==0x00003c66U,
           "UPDATE FP16 rounding: FP32 1.1 should become FP16 bits 0x3c66");
}

/** @brief 检查PROP_MAX_DIFF以及oldMax向下一轮推进。 */
void testPropMaxDiff(){
    resetCmp();

    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)3.0F);

    /* 第一次传播时oldMax仍为复位值-INF。 */
    fsa::CMPTopOutput output =
        runCommand(fsa::CmpControlCmd::PROP_MAX_DIFF);
    expect(output.d_output.valid,
           "PROP_MAX_DIFF first: output should be valid");
    expect(std::isinf(output.d_output.bits) && output.d_output.bits < 0,
           "PROP_MAX_DIFF first: expected -INF-3=-INF");

    /* 上一次PROP_MAX_DIFF已经执行oldMax<-3；本轮newMax更新到5。 */
    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)5.0F);
    output = runCommand(fsa::CmpControlCmd::PROP_MAX_DIFF);
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)-2.0F),
           "PROP_MAX_DIFF second: expected oldMax-newMax=3-5=-2");

    /* oldMax和newMax现在都为5，输入4不能降低newMax。 */
    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)4.0F);
    output = runCommand(fsa::CmpControlCmd::PROP_MAX_DIFF);
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)0.0F),
           "PROP_MAX_DIFF third: expected oldMax-newMax=5-5=0");
}

/** @brief 检查PROP_ZERO不修改最大值状态。 */
void testPropZero(){
    resetCmp();
    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)5.0F);

    fsa::CMPTopOutput output =
        runCommand(fsa::CmpControlCmd::PROP_ZERO,
                   (fsa::acc_t)123.0F);
    expect(output.d_output.valid,
           "PROP_ZERO: output should be valid");
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)0.0F),
           "PROP_ZERO: output should be zero");

    output = runCommand(fsa::CmpControlCmd::PROP_MAX);
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)-5.0F),
           "PROP_ZERO: max state was modified");
}

/** @brief 检查causalCounter的mask和向右递减。 */
void testCausalCounter(){
    resetCmp();
    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)5.0F);

    /* causalCounter非0时，输入9会先被替换成-INF。 */
    fsa::CMPTopOutput output =
        runCommand(fsa::CmpControlCmd::UPDATE,
                   (fsa::acc_t)9.0F,
                   3);

    expect(output.ctrl.valid, "causal mask: ctrl should be valid");
    expect(output.ctrl.bits.causalCounter==2,
           "causal mask: counter 3 should become 2");
    expectUpdateOutput(output,
                       -std::numeric_limits<fsa::acc_t>::infinity(),
                       "causal mask output");

    output = runCommand(fsa::CmpControlCmd::PROP_MAX);
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)-5.0F),
           "causal mask: masked input changed newMax");

    output = runCommand(fsa::CmpControlCmd::PROP_ZERO,
                        (fsa::acc_t)0.0F,
                        1);
    expect(output.ctrl.bits.causalCounter==0,
           "causal counter: 1 should become 0");

    output = runCommand(fsa::CmpControlCmd::PROP_ZERO,
                        (fsa::acc_t)0.0F,
                        0);
    expect(output.ctrl.bits.causalCounter==0,
           "causal counter: 0 should remain 0");

    output = runCommand(fsa::CmpControlCmd::PROP_ZERO,
                        (fsa::acc_t)0.0F,
                        255);
    expect(output.ctrl.bits.causalCounter==254,
           "causal counter: 255 should become 254");
}

/** @brief 检查CMP的RESET命令只清除oldMax和newMax。 */
void testResetCommand(){
    resetCmp();
    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)2.0F);

    const fsa::CMPTopOutput reset_output =
        runCommand(fsa::CmpControlCmd::RESET,
                   (fsa::acc_t)0.0F,
                   2);

    expect(reset_output.ctrl.valid,
           "RESET command: ctrl should still be propagated");
    expect(reset_output.ctrl.bits.cmd==fsa::CmpControlCmd::RESET,
           "RESET command: wrong propagated command");
    expect(reset_output.ctrl.bits.causalCounter==1,
           "RESET command: causalCounter should still decrement");
    expect(!reset_output.d_output.valid,
           "RESET command: d_output should be invalid");

    const fsa::CMPTopOutput output =
        runCommand(fsa::CmpControlCmd::PROP_MAX);
    expect(std::isinf(output.d_output.bits) && output.d_output.bits > 0,
           "RESET command: oldMax/newMax were not reset to -INF");
}

/*
 * 原Chisel工程生成并编码后的8个PWL截距。
 * 位[26:24]保存分段编号，其余相关位保存真正截距的阶码低位和尾数。
 */
const std::uint32_t EXP2_ENCODED_INTERCEPT_BITS[8] = {
    0x00800000U,
    0x017e3c91U,
    0x027b00a2U,
    0x03768dcfU,
    0x04711d65U,
    0x056ae156U,
    0x06640507U,
    0x075cae0fU
};

/** @brief 检查8个PWL截距的顺序、回绕和状态隔离。 */
void testExp2Intercepts(){
    resetCmp();

    /* 连续读取两轮，验证第7项之后自然回绕到第0项。 */
    for(int transaction = 0; transaction < 16; ++transaction){
        const int expected_index = transaction % 8;
        const fsa::CMPTopOutput output =
            runCommand(fsa::CmpControlCmd::PROP_EXP2_INTERCEPTS);

        expect(output.ctrl.valid,
               "PROP_EXP2_INTERCEPTS: ctrl should be valid");
        expect(output.ctrl.bits.cmd==
                   fsa::CmpControlCmd::PROP_EXP2_INTERCEPTS,
               "PROP_EXP2_INTERCEPTS: command was not propagated");
        expect(output.d_output.valid,
               "PROP_EXP2_INTERCEPTS: output should be valid");
        expect(accBits(output.d_output.bits)==
                   EXP2_ENCODED_INTERCEPT_BITS[expected_index],
               "PROP_EXP2_INTERCEPTS transaction " +
                   std::to_string(transaction) +
                   ": wrong encoded intercept bits");
    }

    /* PROP_EXP2_INTERCEPTS只发送常量，不能修改newMax。 */
    resetCmp();
    runCommand(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)5.0F);
    runCommand(fsa::CmpControlCmd::PROP_EXP2_INTERCEPTS);

    fsa::CMPTopOutput output =
        runCommand(fsa::CmpControlCmd::PROP_MAX);
    expect(almostEqual(output.d_output.bits, (fsa::acc_t)-5.0F),
           "PROP_EXP2_INTERCEPTS: max state was modified");

    /*
     * CMP的RESET命令与Chisel一致，只清max，不清独立的exp2_counter。
     * 顶层reset才会把exp2_counter恢复到第0项。
     */
    resetCmp();
    output = runCommand(fsa::CmpControlCmd::PROP_EXP2_INTERCEPTS);
    expect(accBits(output.d_output.bits)==EXP2_ENCODED_INTERCEPT_BITS[0],
           "exp2 counter: first intercept should be index 0");

    runCommand(fsa::CmpControlCmd::RESET);
    output = runCommand(fsa::CmpControlCmd::PROP_EXP2_INTERCEPTS);
    expect(accBits(output.d_output.bits)==EXP2_ENCODED_INTERCEPT_BITS[1],
           "exp2 counter: RESET command should not reset the counter");

    resetCmp();
    output = runCommand(fsa::CmpControlCmd::PROP_EXP2_INTERCEPTS);
    expect(accBits(output.d_output.bits)==EXP2_ENCODED_INTERCEPT_BITS[0],
           "exp2 counter: top reset should restore index 0");
}

}  // namespace

int main(){
    testTopResetAndIdle();
    testUpdateAndPropMax();
    testPropMaxDiff();
    testPropZero();
    testCausalCounter();
    testResetCommand();
    testExp2Intercepts();

    if(failure_count != 0){
        std::cerr << "[FAIL] test_cmp_top: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_cmp_top: complete CMP functional test"
              << std::endl;
    return 0;
}
