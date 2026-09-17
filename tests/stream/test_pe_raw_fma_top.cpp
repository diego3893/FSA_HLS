/**
 * @file test_pe_raw_fma_top.cpp
 * @brief 独立Raw FMA顶层的位精确功能测试
 */
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

#include <utils/x_hls_utils.h>

#include "fsa/stream/pe_raw_fma.hpp"

namespace{

int failures = 0;

float exactHalfToFloat(const std::uint16_t bits){
    const bool sign = (bits&0x8000U)!=0;
    const unsigned exponent = (bits>>10)&0x1fU;
    const unsigned mantissa = bits&0x03ffU;
    float value = 0.0F;
    if(exponent==0x1fU){
        value = mantissa==0
            ? std::numeric_limits<float>::infinity()
            : std::numeric_limits<float>::quiet_NaN();
    }else if(exponent==0){
        value = std::ldexp((float)mantissa, -24);
    }else{
        value = std::ldexp((float)(1024U+mantissa),
                           (int)exponent-25);
    }
    return sign ? -value : value;
}

std::uint16_t halfBits(const fsa::elem_t value){
    return (std::uint16_t)fp_struct<fsa::elem_t>(value).data().to_uint();
}

fsa::acc_t floatFromBits(const std::uint32_t bits){
    const fp_struct<fsa::acc_t> view((ap_uint<32>)bits);
    return view.to_ieee();
}

std::uint32_t floatBits(const fsa::acc_t value){
    return (std::uint32_t)fp_struct<fsa::acc_t>(value).data().to_uint();
}

bool isNan32(const std::uint32_t bits){
    return (bits&0x7f800000U)==0x7f800000U &&
        (bits&0x007fffffU)!=0;
}

bool isNan16(const std::uint16_t bits){
    return (bits&0x7c00U)==0x7c00U && (bits&0x03ffU)!=0;
}

void expectBits(
    const fsa::PeRawFmaOutput& actual,
    const std::uint32_t expected_acc,
    const std::uint16_t expected_elem,
    const bool expected_exp2,
    const char* label,
    const int vector
){
    const std::uint32_t actual_acc = actual.out_acc_bits.to_uint();
    const std::uint16_t actual_elem =
        (std::uint16_t)actual.out_elem_bits.to_uint();
    const bool acc_equal = actual_acc==expected_acc ||
        (isNan32(actual_acc) && isNan32(expected_acc));
    const bool elem_equal = actual_elem==expected_elem ||
        (isNan16(actual_elem) && isNan16(expected_elem));
    if(!acc_equal || !elem_equal ||
            actual.out_exp2!=expected_exp2){
        if(failures<20){
            std::cerr << "[FAIL] " << label << " vector " << vector
                      << ": acc=0x" << std::hex << actual_acc
                      << " expected=0x" << expected_acc
                      << ", elem=0x" << actual_elem
                      << " expected=0x" << expected_elem
                      << std::dec
                      << ", exp2=" << actual.out_exp2
                      << " expected=" << expected_exp2 << std::endl;
        }
        ++failures;
    }
}

fsa::PeRawFmaOutput runTop(
    const std::uint16_t a,
    const std::uint16_t b,
    const std::uint32_t c,
    const bool exp2
){
    fsa::PeRawFmaInput input{};
    input.in_a_bits = a;
    input.in_b_bits = b;
    input.in_c_bits = c;
    input.in_exp2 = exp2;
    fsa::PeRawFmaOutput output{};
    pe_raw_fma_top(input, output);
    return output;
}

void checkMacVector(
    const std::uint16_t a_bits,
    const std::uint16_t b_bits,
    const std::uint32_t c_bits,
    const int vector
){
    const fsa::acc_t a = exactHalfToFloat(a_bits);
    const fsa::acc_t b = exactHalfToFloat(b_bits);
    const fsa::acc_t c = floatFromBits(c_bits);
    const fsa::acc_t expected = std::fma(
        a, b, c
    );
    const std::uint32_t expected_acc = floatBits(expected);
    const std::uint16_t expected_elem =
        halfBits((fsa::elem_t)expected);
    expectBits(
        runTop(a_bits, b_bits, c_bits, false),
        expected_acc, expected_elem, false, "MAC", vector
    );
}

std::uint32_t nextRandom(std::uint32_t& state){
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void testDirectedMac(){
    struct Vector{
        std::uint16_t a;
        std::uint16_t b;
        std::uint32_t c;
    };
    const Vector vectors[] = {
        {0x4000U, 0x4200U, 0x40800000U}, // 2*3+4
        {0xbc00U, 0x4500U, 0xbf000000U}, // -1*5-0.5
        {0x0000U, 0x7bffU, 0x80000000U},
        {0x8000U, 0x3c00U, 0x00000000U},
        {0x0001U, 0x3c00U, 0x00000000U},
        {0x03ffU, 0x0400U, 0x00800000U},
        {0x7bffU, 0x7bffU, 0x00000000U},
        {0x3c00U, 0x3c00U, 0xbf800000U}, // exact cancellation
        {0x3555U, 0x3aabU, 0x3eaaaaabU},
        {0x7c00U, 0x0000U, 0x3f800000U}, // inf*0 -> NaN
        {0x7c00U, 0x3c00U, 0xff800000U}, // inf + -inf
        {0x7e00U, 0x3c00U, 0x00000000U}
    };
    const int count = (int)(sizeof(vectors)/sizeof(vectors[0]));
    for(int index=0; index<count; ++index){
        checkMacVector(
            vectors[index].a,
            vectors[index].b,
            vectors[index].c,
            index
        );
    }
}

void testRandomFiniteMac(){
    std::uint32_t random = 0x13579bdfU;
    constexpr int vector_count = 30000;
    for(int vector=0; vector<vector_count; ++vector){
        std::uint16_t a = (std::uint16_t)nextRandom(random);
        std::uint16_t b = (std::uint16_t)nextRandom(random);
        std::uint32_t c = nextRandom(random);
        // 随机回归只比较有限数；NaN/Inf由定向向量覆盖。
        if((a&0x7c00U)==0x7c00U){
            a &= 0xfbffU;
        }
        if((b&0x7c00U)==0x7c00U){
            b &= 0xfbffU;
        }
        if((c&0x7f800000U)==0x7f800000U){
            c &= 0xff7fffffU;
        }
        checkMacVector(a, b, c, vector);
    }
}

const float EXP2_SLOPES[8] = {
    0.664062500F, 0.608886719F, 0.558105469F, 0.512207031F,
    0.469482422F, 0.430419922F, 0.394775391F, 0.362060547F
};

const std::uint32_t EXP2_ENCODED_INTERCEPT_BITS[8] = {
    0x00800000U, 0x017e3c91U, 0x027b00a2U, 0x03768dcfU,
    0x04711d65U, 0x056ae156U, 0x06640507U, 0x075cae0fU
};

std::uint32_t restoredInterceptBits(const std::uint32_t encoded){
    return (encoded&0x807fffffU) |
        (((0x7eU | ((encoded>>23)&1U))) << 23);
}

int goldPiece(const float x){
    const int integer = (int)std::trunc(x);
    int piece = (int)(std::fabs(x-(float)integer)*8.0F);
    if(piece>7){
        piece = 7;
    }
    return piece;
}

void testExp2(){
    const std::uint16_t x_bits[] = {
        0x0000U, 0xac00U, 0xb200U, 0xb500U,
        0xb880U, 0xba00U, 0xbb00U, 0xbc00U,
        0xbc40U, 0xbe00U, 0xc100U
    };
    const int x_count = (int)(sizeof(x_bits)/sizeof(x_bits[0]));
    int vector = 0;
    for(int x_index=0; x_index<x_count; ++x_index){
        const float x = exactHalfToFloat(x_bits[x_index]);
        const int integer = (int)std::trunc(x);
        const float fractional = x-(float)integer;
        const int matching_piece = goldPiece(x);
        for(int piece=0; piece<8; ++piece, ++vector){
            const std::uint16_t slope_bits = halfBits(
                (fsa::elem_t)EXP2_SLOPES[piece]
            );
            const float slope = exactHalfToFloat(slope_bits);
            const float intercept = floatFromBits(
                restoredInterceptBits(
                    EXP2_ENCODED_INTERCEPT_BITS[piece]
                )
            );
            const float expected = std::ldexp(
                std::fma(fractional, slope, intercept), integer
            );
            expectBits(
                runTop(
                    x_bits[x_index], slope_bits,
                    EXP2_ENCODED_INTERCEPT_BITS[piece], true
                ),
                floatBits(expected),
                halfBits((fsa::elem_t)expected),
                piece==matching_piece,
                "EXP2", vector
            );
        }
    }
}

}  // namespace

int main(){
    testDirectedMac();
    testRandomFiniteMac();
    testExp2();
    if(failures!=0){
        std::cerr << "[FAIL] test_pe_raw_fma_top: " << failures
                  << " mismatches" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_pe_raw_fma_top: 30000 random MAC vectors, "
                 "directed IEEE cases and exp2 mode" << std::endl;
    return 0;
}
