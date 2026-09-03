/**
 * @file test_pe_pwl_param.cpp
 * @brief 验证完整PE路径使用正常intercept和独立PWL编号。
 */

#include <cassert>
#include <cmath>
#include <iostream>

#include "fsa/arithmetic.hpp"

namespace{

void checkPwlSegment(const float input){
    const fsa::elem_t x = (fsa::elem_t)input;
    const fsa::exp2_counter_t target = fsa::pePwlTargetIndex(x);
    bool matched = false;

    for(int piece=0; piece<fsa::exp2PWLPieces; ++piece){
        const fsa::exp2_counter_t index =
            (fsa::exp2_counter_t)piece;
        const fsa::PePwlParam parameter = fsa::pePwlParam(index);
        const fsa::PeMacUnitOutput output =
            fsa::peMacUnitWithPwlParam(
                x,
                fsa::pePwlSlope(index),
                (fsa::acc_t)123.0F,
                true,
                parameter,
                target
            );

        assert(output.out_exp2==(index==target));
        if(output.out_exp2){
            matched = true;
            const float expected = std::exp2((float)x);
            const float actual = (float)output.out_elemType;
            const float relative_error =
                std::fabs(actual-expected)/expected;
            assert(relative_error<=0.002F);
        }
    }
    assert(matched);
}

}  // namespace

int main(){
    const fsa::PePwlParam idle{};
    const fsa::PeMacUnitOutput mac = fsa::peMacUnitWithPwlParam(
        (fsa::elem_t)2.0F,
        (fsa::elem_t)3.0F,
        (fsa::acc_t)4.0F,
        false,
        idle,
        (fsa::exp2_counter_t)0
    );
    assert(std::fabs(mac.out_accType-(fsa::acc_t)10.0F)<1.0e-6F);
    assert(!mac.out_exp2);

    const float segment_midpoints[fsa::exp2PWLPieces] = {
        -0.0625F,
        -0.1875F,
        -0.3125F,
        -0.4375F,
        -0.5625F,
        -0.6875F,
        -0.8125F,
        -0.9375F
    };
    for(const float input : segment_midpoints){
        checkPwlSegment(input);
    }

    std::cout << "[PASS] test_pe_pwl_param: explicit intercept/index and "
                 "shared MAC/PWL interface" << std::endl;
    return 0;
}
