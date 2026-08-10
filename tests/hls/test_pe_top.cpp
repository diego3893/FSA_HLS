/**
 * @file test_pe_top.cpp
 * @brief 单个PE HLS顶层的完整功能测试。
 *
 * 本testbench覆盖：
 * 1. 顶层复位和无效控制信号；
 * 2. 左、上、下三个方向的数据透传；
 * 3. 从左侧和上侧装载PE.reg；
 * 4. 向上、向下的普通MAC以及MAC结果写回PE.reg；
 * 5. exp2的8段PWL常量扫描；
 * 6. exp2Done防止同一个exp2结果被重复写入；
 * 7. 离开exp2模式后，exp2Done能够被清除。
 */
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include <utils/x_hls_utils.h>

#include "fsa/hls/pe_top.hpp"

namespace {

int failure_count = 0;

/**
 * @brief 检查一个测试条件，并在失败时输出具体项目。
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
bool almostEqualAcc(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual-expected) <= (fsa::acc_t)1.0e-6F;
}

/** @brief 读取elem_t的IEEE FP16原始位模式。 */
std::uint16_t elemBits(const fsa::elem_t value){
    const fp_struct<fsa::elem_t> view(value);
    return (std::uint16_t)view.data().to_uint();
}

/** @brief 按FP16位模式比较两个elem_t，避免浮点容差隐藏舍入错误。 */
bool sameElemBits(const fsa::elem_t actual, const fsa::elem_t expected){
    return elemBits(actual)==elemBits(expected);
}

/**
 * @brief 把32位原始位模式装入acc_t。
 *
 * exp2截距的阶码高位保存了分段编号，所以不能把它当作普通数值构造。
 */
fsa::acc_t accFromBits(const std::uint32_t bits){
    const fp_struct<fsa::acc_t> view((ap_uint<32>)bits);
    return view.to_ieee();
}

/** @brief 调用一次PE顶层，对应一次顶层事务。 */
fsa::PETopOutput runPe(const fsa::PETopInput& input){
    fsa::PETopOutput output{};
    pe_top(input, output);
    return output;
}

/** @brief 复位PE的reg和exp2Done。 */
void resetPe(){
    fsa::PETopInput input{};
    input.reset = true;

    const fsa::PETopOutput output = runPe(input);
    expect(!output.ctrl.valid, "reset: ctrl should be invalid");
    expect(!output.r_output.valid, "reset: r_output should be invalid");
    expect(!output.u_output.valid, "reset: u_output should be invalid");
    expect(!output.d_output.valid, "reset: d_output should be invalid");
}

/**
 * @brief 从左侧把一个元素写入PE.reg。
 * @return 装载前PE.reg中的旧值，它会同时从r_output输出
 */
fsa::elem_t loadRegFromLeft(const fsa::elem_t value){
    fsa::PETopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.load_reg_li = true;
    input.l_input.valid = true;
    input.l_input.bits = value;

    const fsa::PETopOutput output = runPe(input);
    expect(output.ctrl.valid, "load_reg_li: ctrl was not propagated");
    expect(output.ctrl.bits.load_reg_li,
           "load_reg_li: control bit was not propagated");
    expect(output.r_output.valid,
           "load_reg_li: old reg should be sent to r_output");
    return output.r_output.bits;
}

/**
 * @brief 读出当前PE.reg，并在事务结束时把reg清零。
 *
 * load_reg_li本拍输出旧reg，拍末再写入左侧输入，所以可以用它读回状态。
 */
fsa::elem_t readRegAndClear(){
    return loadRegFromLeft((fsa::elem_t)0.0F);
}

/** @brief 检查复位、无效控制以及状态保持。 */
void testResetAndIdle(){
    resetPe();
    expect(sameElemBits(readRegAndClear(), (fsa::elem_t)0.0F),
           "reset: reg should be zero");

    loadRegFromLeft((fsa::elem_t)2.0F);

    /* ctrl.valid=false时，即使其余控制位为true，也不能执行任何动作。 */
    fsa::PETopInput input{};
    input.ctrl.valid = false;
    input.ctrl.bits.mac = true;
    input.ctrl.bits.flow_lr = true;
    input.ctrl.bits.update_reg = true;
    input.l_input = fsa::make_valid((fsa::elem_t)9.0F);
    input.d_input = fsa::make_valid((fsa::acc_t)7.0F);

    const fsa::PETopOutput output = runPe(input);
    expect(!output.ctrl.valid, "idle: output ctrl should be invalid");
    expect(!output.r_output.valid, "idle: r_output should be invalid");
    expect(!output.u_output.valid, "idle: u_output should be invalid");
    expect(!output.d_output.valid, "idle: d_output should be invalid");
    expect(sameElemBits(readRegAndClear(), (fsa::elem_t)2.0F),
           "idle: invalid control changed reg");

    loadRegFromLeft((fsa::elem_t)7.0F);
    resetPe();
    expect(sameElemBits(readRegAndClear(), (fsa::elem_t)0.0F),
           "reset: reg was not cleared after holding data");
}

/** @brief 检查左右、上下和下上三个纯透传通路。 */
void testFlowPaths(){
    resetPe();

    fsa::PETopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.flow_lr = true;
    input.l_input = fsa::make_valid((fsa::elem_t)1.5F);

    fsa::PETopOutput output = runPe(input);
    expect(output.ctrl.valid && output.ctrl.bits.flow_lr,
           "flow_lr: control was not propagated");
    expect(output.r_output.valid, "flow_lr: r_output should be valid");
    expect(sameElemBits(output.r_output.bits, (fsa::elem_t)1.5F),
           "flow_lr: wrong data");
    expect(!output.u_output.valid && !output.d_output.valid,
           "flow_lr: vertical outputs should be invalid");

    input = fsa::PETopInput{};
    input.ctrl.valid = true;
    input.ctrl.bits.flow_ud = true;
    input.u_input = fsa::make_valid((fsa::acc_t)2.5F);

    output = runPe(input);
    expect(output.d_output.valid, "flow_ud: d_output should be valid");
    expect(almostEqualAcc(output.d_output.bits, (fsa::acc_t)2.5F),
           "flow_ud: wrong data");
    expect(!output.r_output.valid && !output.u_output.valid,
           "flow_ud: unrelated outputs should be invalid");

    input = fsa::PETopInput{};
    input.ctrl.valid = true;
    input.ctrl.bits.flow_du = true;
    input.d_input = fsa::make_valid((fsa::acc_t)-3.5F);

    output = runPe(input);
    expect(output.u_output.valid, "flow_du: u_output should be valid");
    expect(almostEqualAcc(output.u_output.bits, (fsa::acc_t)-3.5F),
           "flow_du: wrong data");
    expect(!output.r_output.valid && !output.d_output.valid,
           "flow_du: unrelated outputs should be invalid");
}

/** @brief 检查从左侧和上侧装载PE.reg。 */
void testRegisterLoads(){
    resetPe();

    expect(sameElemBits(loadRegFromLeft((fsa::elem_t)4.0F),
                        (fsa::elem_t)0.0F),
           "load_reg_li: first old value should be zero");
    expect(sameElemBits(loadRegFromLeft((fsa::elem_t)7.0F),
                        (fsa::elem_t)4.0F),
           "load_reg_li: old reg was not sent right");
    expect(sameElemBits(readRegAndClear(), (fsa::elem_t)7.0F),
           "load_reg_li: new value was not stored");

    /* FP16 -2.25的位模式是0xc080，把它放在acc_t低16位中。 */
    fsa::PETopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.load_reg_ui = true;
    input.u_input.valid = true;
    input.u_input.bits = accFromBits(0x0000c080U);

    const fsa::PETopOutput output = runPe(input);
    expect(!output.r_output.valid,
           "load_reg_ui: loading from upper input should not flow right");
    expect(sameElemBits(readRegAndClear(), (fsa::elem_t)-2.25F),
           "load_reg_ui: low 16 bits were not loaded into reg");
}

/** @brief 检查两个MAC方向以及update_reg写回。 */
void testMacPaths(){
    resetPe();
    loadRegFromLeft((fsa::elem_t)2.0F);

    /* acc_ui=false：2*3+4=10，结果向上输出。 */
    fsa::PETopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.mac = true;
    input.ctrl.bits.acc_ui = false;
    input.l_input = fsa::make_valid((fsa::elem_t)3.0F);
    input.d_input = fsa::make_valid((fsa::acc_t)4.0F);

    fsa::PETopOutput output = runPe(input);
    expect(output.u_output.valid, "MAC upward: u_output should be valid");
    expect(almostEqualAcc(output.u_output.bits, (fsa::acc_t)10.0F),
           "MAC upward: expected 2*3+4=10");
    expect(!output.d_output.valid, "MAC upward: d_output should be invalid");
    expect(sameElemBits(readRegAndClear(), (fsa::elem_t)2.0F),
           "MAC without update_reg changed reg");

    resetPe();
    loadRegFromLeft((fsa::elem_t)2.0F);

    /* acc_ui=true：2*5+1=11，结果向下输出。 */
    input = fsa::PETopInput{};
    input.ctrl.valid = true;
    input.ctrl.bits.mac = true;
    input.ctrl.bits.acc_ui = true;
    input.l_input = fsa::make_valid((fsa::elem_t)5.0F);
    input.u_input = fsa::make_valid((fsa::acc_t)1.0F);

    output = runPe(input);
    expect(output.d_output.valid, "MAC downward: d_output should be valid");
    expect(almostEqualAcc(output.d_output.bits, (fsa::acc_t)11.0F),
           "MAC downward: expected 2*5+1=11");
    expect(!output.u_output.valid, "MAC downward: u_output should be invalid");

    /* update_reg=true：同一个MAC结果还应在拍末写入reg。 */
    resetPe();
    loadRegFromLeft((fsa::elem_t)2.0F);

    input = fsa::PETopInput{};
    input.ctrl.valid = true;
    input.ctrl.bits.mac = true;
    input.ctrl.bits.acc_ui = false;
    input.ctrl.bits.update_reg = true;
    input.l_input = fsa::make_valid((fsa::elem_t)3.0F);
    input.d_input = fsa::make_valid((fsa::acc_t)4.0F);

    output = runPe(input);
    expect(output.u_output.valid, "update_reg: MAC output should remain valid");
    expect(almostEqualAcc(output.u_output.bits, (fsa::acc_t)10.0F),
           "update_reg: wrong MAC output");
    expect(sameElemBits(readRegAndClear(), (fsa::elem_t)10.0F),
           "update_reg: MAC result was not written into reg");
}

/*
 * 原Chisel工程中，8个斜率以FP16形式从左侧常量通路逐拍进入PE。
 * 数组顺序已经按PE扫描顺序反转：第0项对应[-0.125, 0]，
 * 第7项对应[-1, -0.875]。
 */
const fsa::elem_t EXP2_SLOPES[8] = {
    (fsa::elem_t)0.664062500F,
    (fsa::elem_t)0.608886719F,
    (fsa::elem_t)0.558105469F,
    (fsa::elem_t)0.512207031F,
    (fsa::elem_t)0.469482422F,
    (fsa::elem_t)0.430419922F,
    (fsa::elem_t)0.394775391F,
    (fsa::elem_t)0.362060547F
};

/*
 * CMP向下发送的编码截距。位[26:24]保存分段编号，
 * PE在计算前恢复真正的IEEE FP32阶码。
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

/* 独立保存恢复后的普通FP32截距，供testbench计算金标准。 */
const fsa::acc_t EXP2_GOLD_INTERCEPTS[8] = {
    (fsa::acc_t)1.000000000F,
    (fsa::acc_t)0.993111670F,
    (fsa::acc_t)0.980478406F,
    (fsa::acc_t)0.963101327F,
    (fsa::acc_t)0.941854775F,
    (fsa::acc_t)0.917500854F,
    (fsa::acc_t)0.890701711F,
    (fsa::acc_t)0.862030923F
};

/** @brief testbench独立计算输入x应选择的PWL分段。 */
int goldExp2Piece(const fsa::elem_t x){
    const fsa::acc_t x_acc = (fsa::acc_t)x;
    const int integer_part = (int)std::trunc(x_acc);
    const fsa::acc_t fractional_part = x_acc-(fsa::acc_t)integer_part;
    return (int)(std::fabs(fractional_part)*(fsa::acc_t)8.0F);
}

/**
 * @brief testbench中的独立PWL金标准。
 *
 * 这里不调用DUT中的peExp2PWL()，避免用被测函数检查自己。
 */
fsa::elem_t goldExp2(const fsa::elem_t x){
    const fsa::acc_t x_acc = (fsa::acc_t)x;
    const int integer_part = (int)std::trunc(x_acc);
    const fsa::acc_t fractional_part = x_acc-(fsa::acc_t)integer_part;
    const int piece = goldExp2Piece(x);

    const fsa::acc_t fractional_result = std::fma(
        fractional_part,
        (fsa::acc_t)EXP2_SLOPES[piece],
        EXP2_GOLD_INTERCEPTS[piece]
    );

    return (fsa::elem_t)std::ldexp(fractional_result, integer_part);
}

/**
 * @brief 向PE发送一个编号的斜率和编码截距。
 * @param piece 分段编号
 * @param slope 本拍发送的斜率，可用于构造exp2Done测试
 */
void sendExp2Piece(const int piece, const fsa::elem_t slope){
    fsa::PETopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.exp2 = true;
    input.ctrl.bits.acc_ui = false;
    input.l_input = fsa::make_valid(slope);
    input.d_input = fsa::make_valid(
        accFromBits(EXP2_ENCODED_INTERCEPT_BITS[piece])
    );

    const fsa::PETopOutput output = runPe(input);
    expect(output.ctrl.valid && output.ctrl.bits.exp2,
           "exp2: control was not propagated");
    expect(!output.r_output.valid &&
           !output.u_output.valid &&
           !output.d_output.valid,
           "exp2 without flow/mac should only update internal reg");
}

/** @brief 依次发送8组PWL常量，模拟真实硬件中的逐拍扫描。 */
void sendAllExp2Pieces(){
    for(int piece = 0; piece < 8; ++piece){
        sendExp2Piece(piece, EXP2_SLOPES[piece]);
    }
}

/** @brief 检查8段PWL、边界以及带整数部分的负指数。 */
void testExp2AllPieces(){
    /*
     * 前8项是各段中点；其后包含分段边界和不同整数部分。
     * FSA中exp2输入来自S-newMax，因此正常工作范围为x<=0。
     */
    const fsa::elem_t test_values[] = {
        (fsa::elem_t)-0.0625F,
        (fsa::elem_t)-0.1875F,
        (fsa::elem_t)-0.3125F,
        (fsa::elem_t)-0.4375F,
        (fsa::elem_t)-0.5625F,
        (fsa::elem_t)-0.6875F,
        (fsa::elem_t)-0.8125F,
        (fsa::elem_t)-0.9375F,
        (fsa::elem_t)0.0F,
        (fsa::elem_t)-0.125F,
        (fsa::elem_t)-0.250F,
        (fsa::elem_t)-0.375F,
        (fsa::elem_t)-0.500F,
        (fsa::elem_t)-0.625F,
        (fsa::elem_t)-0.750F,
        (fsa::elem_t)-0.875F,
        (fsa::elem_t)-1.0F,
        (fsa::elem_t)-1.0625F,
        (fsa::elem_t)-1.5F,
        (fsa::elem_t)-2.5F
    };

    const int test_count =
        (int)(sizeof(test_values)/sizeof(test_values[0]));

    for(int test = 0; test < test_count; ++test){
        const fsa::elem_t x = test_values[test];

        resetPe();
        loadRegFromLeft(x);
        sendAllExp2Pieces();

        const fsa::elem_t actual = readRegAndClear();
        const fsa::elem_t expected = goldExp2(x);

        expect(
            sameElemBits(actual, expected),
            "exp2 vector " + std::to_string(test) +
            ": FP16 result bits do not match PWL gold"
        );
    }
}

/** @brief 检查exp2Done的置位、保护和清除行为。 */
void testExp2Done(){
    const fsa::elem_t x = (fsa::elem_t)-0.3125F;
    const int matching_piece = goldExp2Piece(x);
    const fsa::elem_t expected = goldExp2(x);

    resetPe();
    loadRegFromLeft(x);
    sendAllExp2Pieces();

    /*
     * 再发送一次匹配截距，但故意把斜率改成0。
     * 如果exp2Done没有阻止重复写入，reg会被错误结果覆盖。
     */
    sendExp2Piece(matching_piece, (fsa::elem_t)0.0F);
    expect(sameElemBits(readRegAndClear(), expected),
           "exp2Done: duplicate matching piece overwrote reg");

    /* 重新得到一个正确结果，然后用一个非exp2事务清除exp2Done。 */
    resetPe();
    loadRegFromLeft(x);
    sendAllExp2Pieces();

    fsa::PETopInput leave_exp2{};
    leave_exp2.ctrl.valid = true;
    runPe(leave_exp2);

    /* exp2Done清除后，同一个匹配段应允许再次写入。 */
    sendExp2Piece(matching_piece, (fsa::elem_t)0.0F);
    const fsa::elem_t rewritten = readRegAndClear();
    const fsa::elem_t rewritten_expected =
        (fsa::elem_t)EXP2_GOLD_INTERCEPTS[matching_piece];

    expect(sameElemBits(rewritten, rewritten_expected),
           "exp2Done: leaving exp2 mode did not enable the next exp2 write");
}

}  // namespace

int main(){
    testResetAndIdle();
    testFlowPaths();
    testRegisterLoads();
    testMacPaths();
    testExp2AllPieces();
    testExp2Done();

    if(failure_count != 0){
        std::cerr << "[FAIL] test_pe_top: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_pe_top: complete PE functional test"
              << std::endl;
    return 0;
}
