/**
 * @file test_output_delayer_top.cpp
 * @brief OutputDelayer HLS顶层完整功能测试。
 *
 * OutputDelayer的等效行为是：第0路延迟3拍、第1路延迟2拍、
 * 第2路延迟1拍、第3路不延迟。本测试检查复位和连续多拍对齐结果。
 */
#include <cmath>
#include <iostream>
#include <string>

#include "fsa/hls/output_delayer_top.hpp"

namespace {

static_assert(fsa::SA_COLS==4,
              "test_output_delayer_top currently expects SA_COLS=4");

int failure_count = 0;

void expect(const bool condition, const std::string& message){
    if(!condition){
        std::cerr << "[FAIL] " << message << std::endl;
        ++failure_count;
    }
}

/** @brief 构造4路FP32输入向量。 */
fsa::AccVector makeVector(const float v0,
                          const float v1,
                          const float v2,
                          const float v3){
    fsa::AccVector result{};
    result[0] = (fsa::acc_t)v0;
    result[1] = (fsa::acc_t)v1;
    result[2] = (fsa::acc_t)v2;
    result[3] = (fsa::acc_t)v3;
    return result;
}

void expectVector(const fsa::AccVector& actual,
                  const fsa::AccVector& expected,
                  const std::string& name){
    for(std::size_t lane=0; lane<(std::size_t)fsa::SA_COLS; ++lane){
        const float difference = std::fabs(
            (float)actual[lane]-(float)expected[lane]
        );
        expect(difference <= 1.0e-6F,
               name + ": lane " + std::to_string(lane) + " mismatch");
    }
}

fsa::OutputDelayerTopOutput runOutputDelayer(
    const fsa::OutputDelayerTopInput& input){
    fsa::OutputDelayerTopOutput output{};
    output_delayer_top(input, output);
    return output;
}

void resetOutputDelayer(){
    fsa::OutputDelayerTopInput input{};
    input.reset = true;

    const fsa::OutputDelayerTopOutput output = runOutputDelayer(input);
    expectVector(output.out, makeVector(0, 0, 0, 0), "reset");
}

fsa::AccVector sendInput(const fsa::AccVector& data){
    fsa::OutputDelayerTopInput input{};
    input.in = data;
    return runOutputDelayer(input).out;
}

/** @brief 检查对SA底部各列输出的反向阶梯延迟。 */
void testOutputAlignment(){
    resetOutputDelayer();

    expectVector(
        sendInput(makeVector(10, 20, 30, 40)),
        makeVector(0, 0, 0, 40),
        "align cycle 0"
    );
    expectVector(
        sendInput(makeVector(11, 21, 31, 41)),
        makeVector(0, 0, 30, 41),
        "align cycle 1"
    );
    expectVector(
        sendInput(makeVector(12, 22, 32, 42)),
        makeVector(0, 20, 31, 42),
        "align cycle 2"
    );
    expectVector(
        sendInput(makeVector(13, 23, 33, 43)),
        makeVector(10, 21, 32, 43),
        "align cycle 3"
    );
    expectVector(
        sendInput(makeVector(14, 24, 34, 44)),
        makeVector(11, 22, 33, 44),
        "align cycle 4"
    );
}

}  // namespace

int main(){
    testOutputAlignment();

    if(failure_count != 0){
        std::cerr << "[FAIL] test_output_delayer_top: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_output_delayer_top: complete functional test"
              << std::endl;
    return 0;
}
