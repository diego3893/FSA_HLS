/**
 * @file test_pe_pwl_param.cpp
 * @brief 验证PE恢复使用原始编码intercept，并与普通MAC共享计算入口。
 */

#include <cassert>
#include <cmath>
#include <iostream>
#include <utils/x_hls_utils.h>

#include "fsa/arithmetic.hpp"

namespace{

const fsa::elem_t EXP2_SLOPES[fsa::exp2PWLPieces] = {
    (fsa::elem_t)0.664062500F,
    (fsa::elem_t)0.608886719F,
    (fsa::elem_t)0.558105469F,
    (fsa::elem_t)0.512207031F,
    (fsa::elem_t)0.469482422F,
    (fsa::elem_t)0.430419922F,
    (fsa::elem_t)0.394775391F,
    (fsa::elem_t)0.362060547F
};

void checkPwlSegment(const float input){
    const fsa::elem_t x = (fsa::elem_t)input;
    bool matched = false;

    for(int piece=0; piece<fsa::exp2PWLPieces; ++piece){
        const fsa::PeMacUnitOutput output = fsa::peMacUnit(
            x,
            EXP2_SLOPES[piece],
            fsa::exp2PWLIntercept((fsa::exp2_counter_t)piece),
            true
        );

        if(output.out_exp2){
            assert(!matched);
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

void checkAllFiniteSoftmaxInputs(){
    for(unsigned bits=0x8000U; bits<0xfc00U; ++bits){
        const fp_struct<fsa::elem_t> view((ap_uint<16>)bits);
        const fsa::elem_t x = view.to_ieee();
        if((float)x < -16.0F){
            continue;
        }

        int matches = 0;
        int matched_piece = -1;
        for(int piece=0; piece<fsa::exp2PWLPieces; ++piece){
            const fsa::PeMacUnitOutput output = fsa::peMacUnit(
                x,
                EXP2_SLOPES[piece],
                fsa::exp2PWLIntercept((fsa::exp2_counter_t)piece),
                true
            );
            if(output.out_exp2){
                ++matches;
                matched_piece = piece;
            }
        }
        assert(matches==1);
        int expected_piece = (int)(
            std::fabs((float)x-std::trunc((float)x))*
            (float)fsa::exp2PWLPieces
        );
        if(expected_piece>=fsa::exp2PWLPieces){
            expected_piece = fsa::exp2PWLPieces-1;
        }
        assert(matched_piece==expected_piece);
    }

    const fp_struct<fsa::elem_t> negative_infinity((ap_uint<16>)0xfc00);
    for(int piece=0; piece<fsa::exp2PWLPieces; ++piece){
        const fsa::PeMacUnitOutput output = fsa::peMacUnit(
            negative_infinity.to_ieee(),
            EXP2_SLOPES[piece],
            fsa::exp2PWLIntercept((fsa::exp2_counter_t)piece),
            true
        );
        assert((float)output.out_elemType==0.0F);
    }
}

}  // namespace

int main(){
    const fsa::PeMacUnitOutput mac = fsa::peMacUnit(
        (fsa::elem_t)2.0F,
        (fsa::elem_t)3.0F,
        (fsa::acc_t)4.0F,
        false
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
    checkAllFiniteSoftmaxInputs();

    std::cout << "[PASS] test_pe_pwl_param: encoded intercept and shared "
                 "MAC/PWL interface" << std::endl;
    return 0;
}
