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

std::uint16_t goldHalfBitsFromFloatBits(const std::uint32_t bits){
    const std::uint16_t sign = (std::uint16_t)((bits>>16)&0x8000U);
    const unsigned exponent_bits = (bits>>23)&0xffU;
    const unsigned mantissa = bits&0x007fffffU;
    if(exponent_bits==0xffU){
        return (std::uint16_t)(sign | 0x7c00U |
            (mantissa!=0 ? 0x03ffU : 0U));
    }
    if(exponent_bits==0){
        return sign;
    }

    const int exponent = (int)exponent_bits-127;
    if(exponent>15){
        return (std::uint16_t)(sign | 0x7c00U);
    }
    if(exponent<-14){
        // 与Vitis half转换一致：FP16非规格化输出冲刷为带符号零。
        return sign;
    }

    unsigned rounded = mantissa>>13;
    const unsigned remainder = mantissa&0x1fffU;
    if(remainder>0x1000U ||
            (remainder==0x1000U && (rounded&1U)!=0)){
        ++rounded;
    }
    int half_exponent = exponent+15;
    if((rounded&0x400U)!=0){
        rounded = 0;
        ++half_exponent;
    }
    if(half_exponent>=0x1f){
        return (std::uint16_t)(sign | 0x7c00U);
    }
    return (std::uint16_t)(sign |
        ((unsigned)half_exponent<<10) | rounded);
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
        goldHalfBitsFromFloatBits(expected_acc);
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
        {0x0000U, 0x3c00U, 0x3f1c70e4U}, // normal RNE
        {0x0000U, 0x3c00U, 0xb62861bcU}, // signed FTZ
        {0x0000U, 0x3c00U, 0x3f801000U}, // RNE tie to even
        {0x0000U, 0x3c00U, 0x3f803000U}, // RNE tie from odd
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

void testAlignmentBoundaries(){
    // 从公开顶层验证对齐和sticky逻辑，不复制被测实现作为golden。
    // 扫过FP32全部有限指数及稀疏/稠密尾数，覆盖移位0、1..26、>=27，
    // 同时覆盖FP16非规格化乘积、异号相减和两种sticky取值。
    const std::uint16_t operands[][2] = {
        {0x3c00U, 0x3c00U}, {0x3c01U, 0x3bffU},
        {0x0001U, 0x0001U}, {0x03ffU, 0x0401U},
        {0x7bffU, 0x7bffU}, {0x3555U, 0x3aabU}
    };
    const std::uint32_t mantissas[] = {
        0U, 1U, 0x00400000U, 0x007fffffU
    };
    int vector = 0;
    for(const auto& pair : operands){
        for(unsigned exponent=0; exponent<255; ++exponent){
            for(const auto mantissa : mantissas){
                for(unsigned signs=0; signs<4; ++signs){
                    const std::uint16_t a = (std::uint16_t)(
                        pair[0] | ((signs&1U)<<15)
                    );
                    const std::uint32_t c = (exponent<<23) |
                        mantissa | ((signs>>1)<<31);
                    checkMacVector(a, pair[1], c, vector++);
                }
            }
        }
    }
    std::cout << "[INFO] alignment boundary MAC vectors: "
              << vector << std::endl;
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
        0xbc40U, 0xbe00U, 0xc100U,
        0x8001U, 0x83ffU, 0x8400U, 0xbbffU, 0xbc01U, 0xc4ffU
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
                goldHalfBitsFromFloatBits(floatBits(expected)),
                piece==matching_piece,
                "EXP2", vector
            );
        }
    }
}

void checkExp2ScaleVector(
    const std::uint16_t x_bits,
    const std::uint16_t slope_bits,
    const std::uint32_t encoded_intercept,
    const int vector
){
    const float x = exactHalfToFloat(x_bits);
    const int integer = (int)std::trunc(x);
    const float expected = std::ldexp(
        std::fma(x-(float)integer, exactHalfToFloat(slope_bits),
                 floatFromBits(restoredInterceptBits(encoded_intercept))),
        integer
    );
    const std::uint32_t expected_bits = floatBits(expected);
    expectBits(
        runTop(x_bits, slope_bits, encoded_intercept, true),
        expected_bits, goldHalfBitsFromFloatBits(expected_bits),
        (int)((encoded_intercept>>24)&7U)==goldPiece(x),
        "EXP2 scale", vector
    );
}

void testExp2ScaleBoundaries(){
    int vector = 0;
    // 整数x令小数乘积为零，从公开顶层独立激励缩放路径。
    // 覆盖正常数、所有1..24位下溢移位、超过24位、正负零及溢出。
    for(int integer=-153; integer<=130; ++integer){
        if(integer>-120 && integer<126){
            continue;
        }
        for(unsigned base_exponent=126; base_exponent<=127;
                ++base_exponent){
            const int distance = 1-(int)base_exponent-integer;
            const unsigned tie_bit = distance>=1 && distance<=24
                ? (1U<<(distance-1)) : 1U;
            const unsigned odd_bit = distance>=1 && distance<=24
                ? (1U<<distance) : 2U;
            const unsigned mantissas[] = {
                0U, 1U, 2U, 3U, 0x003fffffU, 0x007ffffeU, 0x007fffffU,
                tie_bit-1U, tie_bit, tie_bit+1U,
                odd_bit+tie_bit-1U, odd_bit+tie_bit, odd_bit+tie_bit+1U
            };
            for(const auto mantissa : mantissas){
                for(unsigned sign=0; sign<2; ++sign){
                    const std::uint32_t encoded = (sign<<31) |
                        ((base_exponent&1U)<<23) | (mantissa&0x007fffffU);
                    checkExp2ScaleVector(
                        halfBits((fsa::elem_t)integer), 0U, encoded,
                        vector++
                    );
                }
            }
        }
    }
    // 使用真正的PWL斜率/截距，让FMA舍入结果再进入缩放下溢路径。
    // 此范围的1/8步长可以被FP16精确表示。
    for(int eighths=-1280; eighths<=-960; ++eighths){
        const auto x_bits = halfBits((fsa::elem_t)((float)eighths/8.0F));
        for(int piece=0; piece<8; ++piece){
            checkExp2ScaleVector(
                x_bits, halfBits((fsa::elem_t)EXP2_SLOPES[piece]),
                EXP2_ENCODED_INTERCEPT_BITS[piece], vector++
            );
        }
    }
    std::cout << "[INFO] exp2 scale boundary vectors: "
              << vector << std::endl;
}

}  // namespace

int main(){
    testDirectedMac();
    testRandomFiniteMac();
    testAlignmentBoundaries();
    testExp2();
    testExp2ScaleBoundaries();
    if(failures!=0){
        std::cerr << "[FAIL] test_pe_raw_fma_top: " << failures
                  << " mismatches" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_pe_raw_fma_top: 30000 random MAC vectors, "
                 "alignment sweep, directed IEEE cases and exp2 mode"
              << std::endl;
    return 0;
}
