/**
 * @file test_input_delayer_top.cpp
 * @brief InputDelayer HLS顶层完整功能测试。
 *
 * 测试内容：
 * 1. 顶层复位；
 * 2. 不延迟时的直通；
 * 3. rev_input和rev_output的四种组合；
 * 4. 第i路延迟i拍的阶梯延迟；
 * 5. 反转和阶梯延迟同时启用。
 */
#include <cmath>
#include <iostream>
#include <string>

#include "fsa/hls/input_delayer_top.hpp"

namespace {

static_assert(fsa::SA_ROWS==4,
              "test_input_delayer_top currently expects SA_ROWS=4");

int failure_count = 0;

void expect(const bool condition, const std::string& message){
    if(!condition){
        std::cerr << "[FAIL] " << message << std::endl;
        ++failure_count;
    }
}

/** @brief 构造4路FP16输入向量。 */
fsa::ElemVector makeVector(const float v0,
                           const float v1,
                           const float v2,
                           const float v3){
    fsa::ElemVector result{};
    result[0] = (fsa::elem_t)v0;
    result[1] = (fsa::elem_t)v1;
    result[2] = (fsa::elem_t)v2;
    result[3] = (fsa::elem_t)v3;
    return result;
}

/** @brief 比较两个FP16向量。测试值均可由FP16精确表示。 */
void expectVector(const fsa::ElemVector& actual,
                  const fsa::ElemVector& expected,
                  const std::string& name){
    for(std::size_t lane=0; lane<(std::size_t)fsa::SA_ROWS; ++lane){
        const float difference = std::fabs(
            (float)actual[lane]-(float)expected[lane]
        );
        expect(difference==0.0F,
               name + ": lane " + std::to_string(lane) + " mismatch");
    }
}

fsa::InputDelayerTopOutput runInputDelayer(
    const fsa::InputDelayerTopInput& input){
    fsa::InputDelayerTopOutput output{};
    input_delayer_top(input, output);
    return output;
}

/** @brief 复位顶层状态并检查复位输出。 */
void resetInputDelayer(){
    fsa::InputDelayerTopInput input{};
    input.reset = true;

    const fsa::InputDelayerTopOutput output = runInputDelayer(input);
    expectVector(output.out, makeVector(0, 0, 0, 0), "reset");
}

/** @brief 发送一拍有效输入。 */
fsa::ElemVector sendInput(const fsa::ElemVector& data,
                          const bool rev_input,
                          const bool delay_output,
                          const bool rev_output){
    fsa::InputDelayerTopInput input{};
    input.in.valid = true;
    input.in.bits.data = data;
    input.in.bits.rev_input = rev_input;
    input.in.bits.delay_output = delay_output;
    input.in.bits.rev_output = rev_output;
    return runInputDelayer(input).out;
}

/** @brief 检查不启用延迟时的反转组合。 */
void testBypassAndReverse(){
    resetInputDelayer();

    const fsa::ElemVector input = makeVector(1, 2, 3, 4);

    expectVector(
        sendInput(input, false, false, false),
        makeVector(1, 2, 3, 4),
        "bypass"
    );

    expectVector(
        sendInput(input, true, false, false),
        makeVector(4, 3, 2, 1),
        "rev_input"
    );

    expectVector(
        sendInput(input, false, false, true),
        makeVector(4, 3, 2, 1),
        "rev_output"
    );

    expectVector(
        sendInput(input, true, false, true),
        makeVector(1, 2, 3, 4),
        "rev_input_and_output"
    );
}

/** @brief 检查第i路延迟i拍。 */
void testStairDelay(){
    resetInputDelayer();

    expectVector(
        sendInput(makeVector(10, 20, 30, 40), false, true, false),
        makeVector(10, 0, 0, 0),
        "stair cycle 0"
    );
    expectVector(
        sendInput(makeVector(11, 21, 31, 41), false, true, false),
        makeVector(11, 20, 0, 0),
        "stair cycle 1"
    );
    expectVector(
        sendInput(makeVector(12, 22, 32, 42), false, true, false),
        makeVector(12, 21, 30, 0),
        "stair cycle 2"
    );
    expectVector(
        sendInput(makeVector(13, 23, 33, 43), false, true, false),
        makeVector(13, 22, 31, 40),
        "stair cycle 3"
    );
    expectVector(
        sendInput(makeVector(14, 24, 34, 44), false, true, false),
        makeVector(14, 23, 32, 41),
        "stair cycle 4"
    );
}

/** @brief 检查输入反转、阶梯延迟和输出反转同时启用。 */
void testReverseAndDelay(){
    resetInputDelayer();

    expectVector(
        sendInput(makeVector(1, 2, 3, 4), true, true, true),
        makeVector(0, 0, 0, 4),
        "reverse delay cycle 0"
    );
    expectVector(
        sendInput(makeVector(5, 6, 7, 8), true, true, true),
        makeVector(0, 0, 3, 8),
        "reverse delay cycle 1"
    );
    expectVector(
        sendInput(makeVector(9, 10, 11, 12), true, true, true),
        makeVector(0, 2, 7, 12),
        "reverse delay cycle 2"
    );
    expectVector(
        sendInput(makeVector(13, 14, 15, 16), true, true, true),
        makeVector(1, 6, 11, 16),
        "reverse delay cycle 3"
    );
}

}  // namespace

int main(){
    testBypassAndReverse();
    testStairDelay();
    testReverseAndDelay();

    if(failure_count != 0){
        std::cerr << "[FAIL] test_input_delayer_top: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_input_delayer_top: complete functional test"
              << std::endl;
    return 0;
}
