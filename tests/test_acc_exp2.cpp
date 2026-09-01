/**
 * @file test_acc_exp2.cpp
 * @brief Accumulator 8段PWL exp2的独立数值测试。
 *
 * std::exp2()只在测试中生成数学参考值，不进入待综合实现。
 */

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

#include "fsa/arithmetic.hpp"

namespace{

constexpr fsa::acc_t MAX_RELATIVE_ERROR = (fsa::acc_t)1.0e-3F;

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

    const fsa::elem_t pe_actual =
        fsa::peExp2Approx((fsa::elem_t)x);
    const fsa::acc_t pe_relative_error =
        std::fabs((fsa::acc_t)pe_actual-expected)/std::fabs(expected);
    assert(pe_relative_error <= (fsa::acc_t)5.0e-3F);
}

}  // namespace

int main(){
    assert((fsa::acc_t)fsa::peExp2Approx(
        (fsa::elem_t)-std::numeric_limits<float>::infinity()
    ) == (fsa::acc_t)0.0F);
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
        checkExp2(x);
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

    std::cout << "[PASS] test_acc_exp2: integers, 8 midpoints, "
                 "boundaries, mixed inputs" << std::endl;
    return 0;
}
