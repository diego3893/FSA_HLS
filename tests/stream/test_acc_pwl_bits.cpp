/**
 * @file test_acc_pwl_bits.cpp
 * @brief 验证stream路径Accumulator的位域PWL前处理与数值结果。
 */

#include <cmath>
#include <iostream>

#include "fsa/stream/arithmetic.hpp"

namespace{

    int failures = 0;

    void checkExp2(const fsa::acc_t x){
        const fsa::AccPwlInput prepared = fsa::prepareAccPwlInput(x);
        const fsa::acc_t actual = fsa::accExp2PWL(x);
        const fsa::acc_t expected = (fsa::acc_t)std::exp2((double)x);
        const fsa::acc_t relative_error = expected==0.0F
            ? std::fabs(actual-expected)
            : std::fabs(actual-expected)/std::fabs(expected);

        if(relative_error>1.0e-3F){
            std::cerr << "[FAIL] x=" << x
                      << ", integer=" << prepared.integer
                      << ", fractional=" << prepared.fractional
                      << ", actual=" << actual
                      << ", expected=" << expected
                      << ", relative_error=" << relative_error
                      << std::endl;
            ++failures;
        }
    }

}  // namespace

int main(){
    const fsa::acc_t inputs[] = {
        0.0F, -0.0F,
        -0.0625F, -0.125F, -0.1875F, -0.25F,
        -0.3125F, -0.375F, -0.4375F, -0.5F,
        -0.5625F, -0.625F, -0.6875F, -0.75F,
        -0.8125F, -0.875F, -0.9375F,
        -1.0F, -1.5F, -2.375F, -3.8125F, -5.0F,
        -0.0009765625F, -7.9990234375F
    };

    for(const fsa::acc_t input : inputs){
        checkExp2(input);
    }

    if(failures!=0){
        return 1;
    }
    std::cout << "[PASS] stream Acc PWL bit preprocessing" << std::endl;
    return 0;
}
