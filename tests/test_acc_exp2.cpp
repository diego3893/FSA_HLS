/**
 * @file test_acc_exp2.cpp
 * @brief Accumulator 8段PWL exp2的独立数值测试。
 *
 * std::exp2()只在测试中生成数学参考值，不进入待综合实现。
 */

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "fsa/arithmetic.hpp"

namespace{

constexpr fsa::acc_t MAX_RELATIVE_ERROR = (fsa::acc_t)1.0e-3F;

std::uint32_t floatBits(const fsa::acc_t value){
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void checkExp2Bits(const fsa::acc_t x, const std::uint32_t expected_bits){
    const fsa::acc_t actual = fsa::accExp2PWL(x);
    assert(floatBits(actual)==expected_bits);
}

void checkExp2(const fsa::acc_t x){
    const fsa::acc_t actual = fsa::accExp2PWL(x);
    const fsa::acc_t expected =
        (fsa::acc_t)std::exp2((double)x);
    const fsa::acc_t relative_error =
        std::fabs(actual-expected)/std::fabs(expected);

    std::cout << "x=" << x
              << ", actual=" << actual
              << ", expected=" << expected
              << ", relative_error=" << relative_error
              << std::endl;

    assert(relative_error <= MAX_RELATIVE_ERROR);
}

}  // namespace

int main(){
    // 整数输入：验证整数部分拆分和ldexp指数恢复。
    const fsa::acc_t integer_inputs[] = {
        (fsa::acc_t)0.0F,
        (fsa::acc_t)-1.0F,
        (fsa::acc_t)-2.0F,
        (fsa::acc_t)-3.0F,
        (fsa::acc_t)-5.0F
    };

    for(const fsa::acc_t x : integer_inputs){
        checkExp2(x);
    }

    // 8个分段的中点：保证每一组slope/intercept都会被读取。
    const fsa::acc_t segment_midpoints[fsa::exp2PWLPieces] = {
        (fsa::acc_t)-0.0625F,
        (fsa::acc_t)-0.1875F,
        (fsa::acc_t)-0.3125F,
        (fsa::acc_t)-0.4375F,
        (fsa::acc_t)-0.5625F,
        (fsa::acc_t)-0.6875F,
        (fsa::acc_t)-0.8125F,
        (fsa::acc_t)-0.9375F
    };

    for(const fsa::acc_t x : segment_midpoints){
        checkExp2(x);
    }

    // 相邻分段的7个边界：验证分段编号切换不会造成明显跳变。
    const fsa::acc_t segment_boundaries[] = {
        (fsa::acc_t)-0.125F,
        (fsa::acc_t)-0.250F,
        (fsa::acc_t)-0.375F,
        (fsa::acc_t)-0.500F,
        (fsa::acc_t)-0.625F,
        (fsa::acc_t)-0.750F,
        (fsa::acc_t)-0.875F
    };

    for(const fsa::acc_t x : segment_boundaries){
        // MOD: 同时检查每个分段边界的左右相邻FP32值，捕获索引跳变。
        checkExp2(std::nextafter(
            x,
            -std::numeric_limits<fsa::acc_t>::infinity()
        ));
        checkExp2(x);
        checkExp2(std::nextafter(
            x,
            std::numeric_limits<fsa::acc_t>::infinity()
        ));
    }

    // 同时带整数和小数部分的输入。
    const fsa::acc_t mixed_inputs[] = {
        (fsa::acc_t)-1.5F,
        (fsa::acc_t)-2.375F,
        (fsa::acc_t)-3.8125F
    };

    for(const fsa::acc_t x : mixed_inputs){
        checkExp2(x);
    }

    // MOD: 覆盖浮点转int前必须拦截的Inf、NaN和指数上下溢边界。
    checkExp2Bits(-std::numeric_limits<fsa::acc_t>::infinity(), 0x00000000U);
    checkExp2Bits(std::numeric_limits<fsa::acc_t>::infinity(), 0x7f800000U);
    checkExp2Bits(std::numeric_limits<fsa::acc_t>::quiet_NaN(), 0x7fc00000U);
    checkExp2Bits((fsa::acc_t)128.0F, 0x7f800000U);
    checkExp2Bits((fsa::acc_t)-126.0F, 0x00800000U);
    checkExp2Bits((fsa::acc_t)-127.0F, 0x00400000U);
    checkExp2Bits((fsa::acc_t)-149.0F, 0x00000001U);
    checkExp2Bits((fsa::acc_t)-150.0F, 0x00000000U);

    std::cout << "[PASS] test_acc_exp2: integers, 8 midpoints, "
                 "boundaries, mixed inputs, IEEE limits" << std::endl;
    return 0;
}
