#include <cfenv>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "fsa/stream/fp32_raw_fma.hpp"

namespace{

unsigned failures = 0;
unsigned checks = 0;

#ifdef FSA_CHECK_HOST_FMA
float fromBits(const std::uint32_t bits){
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t toBits(const float value){
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
#endif

bool isNan(const std::uint32_t bits){
    return (bits&0x7f800000U)==0x7f800000U && (bits&0x007fffffU)!=0;
}

// 独立golden：18个32位字覆盖2^-298到2^277的精确整数网格。
// 与DUT的51位对阶/sticky算法不同，此处在最终舍入前不截断任何位。
using Exact = std::array<std::uint32_t, 18>;

bool exactBit(const Exact& value, const int bit){
    return bit>=0 && bit<576 && ((value[bit/32]>>(bit%32))&1U)!=0;
}

Exact placeExact(std::uint64_t value, const int shift){
    Exact result{};
    for(int bit=0; bit<48; ++bit){
        if(((value>>bit)&1U)!=0){
            result[(shift+bit)/32] |= 1U<<((shift+bit)%32);
        }
    }
    return result;
}

std::uint32_t exactFma(const std::uint32_t a, const std::uint32_t b,
                       const std::uint32_t c){
    const unsigned ea = (a>>23)&255U, eb = (b>>23)&255U, ec = (c>>23)&255U;
    const bool ps = ((a^b)>>31)!=0, cs = (c>>31)!=0;
    const bool az = (a&0x7fffffffU)==0, bz = (b&0x7fffffffU)==0;
    const bool cz = (c&0x7fffffffU)==0;
    if(isNan(a) || isNan(b) || isNan(c) ||
            (ea==255 && bz) || (eb==255 && az) ||
            ((ea==255 || eb==255) && ec==255 && ps!=cs)){
        return 0x7fc00000U;
    }
    if(ea==255 || eb==255){ return (ps ? 0xff800000U : 0x7f800000U); }
    if(ec==255){ return c; }
    if(az || bz){ return cz ? ((ps && cs) ? 0x80000000U : 0U) : c; }

    const std::uint64_t sa = (a&0x7fffffU)|(ea!=0 ? 0x800000U : 0U);
    const std::uint64_t sb = (b&0x7fffffU)|(eb!=0 ? 0x800000U : 0U);
    const std::uint64_t sc = (c&0x7fffffU)|(ec!=0 ? 0x800000U : 0U);
    const int la = ea==0 ? -149 : (int)ea-150;
    const int lb = eb==0 ? -149 : (int)eb-150;
    const int lc = ec==0 ? -149 : (int)ec-150;
    Exact p = placeExact(sa*sb, la+lb+298);
    Exact q = placeExact(sc, lc+298);
    Exact sum{};
    bool sign = ps;
    if(ps==cs){
        std::uint64_t carry = 0;
        for(int word=0; word<18; ++word){
            const std::uint64_t wide = (std::uint64_t)p[word]+q[word]+carry;
            sum[word] = (std::uint32_t)wide;
            carry = wide>>32;
        }
    }else{
        int order = 0;
        for(int word=17; word>=0 && order==0; --word){
            if(p[word]!=q[word]){ order = p[word]>q[word] ? 1 : -1; }
        }
        if(order==0){ return 0U; }
        if(order<0){ p.swap(q); sign = cs; }
        std::uint64_t borrow = 0;
        for(int word=0; word<18; ++word){
            const std::uint64_t sub = (std::uint64_t)q[word]+borrow;
            sum[word] = (std::uint32_t)((std::uint64_t)p[word]-sub);
            borrow = (std::uint64_t)p[word]<sub ? 1 : 0;
        }
    }
    int highest = 575;
    while(highest>=0 && !exactBit(sum, highest)){ --highest; }
    if(highest<0){ return 0U; }
    int top_exponent = highest-298;
    const int shift = highest>=172 ? highest-23 : 149;
    std::uint32_t rounded = 0;
    for(int bit=0; bit<24; ++bit){
        if(exactBit(sum, shift+bit)){ rounded |= 1U<<bit; }
    }
    bool sticky = false;
    for(int bit=0; bit<shift-1; ++bit){ sticky = sticky || exactBit(sum, bit); }
    if(exactBit(sum, shift-1) && (sticky || (rounded&1U)!=0)){ ++rounded; }
    if((rounded&0x1000000U)!=0){ rounded >>= 1; ++top_exponent; }
    const std::uint32_t sign_bit = sign ? 0x80000000U : 0U;
    if(top_exponent>127){ return sign_bit|0x7f800000U; }
    if((rounded&0x800000U)==0){ return sign_bit|rounded; }
    const unsigned exponent = top_exponent < -126 ? 1U : (unsigned)(top_exponent+127);
    return sign_bit|(exponent<<23)|(rounded&0x7fffffU);
}

void checkExpected(
    const std::uint32_t a, const std::uint32_t b, const std::uint32_t c,
    const std::uint32_t expected, const char* label
){
    fsa::Fp32RawFmaInput input{};
    input.a_bits = a;
    input.b_bits = b;
    input.c_bits = c;
    ap_uint<32> output = 0;
    fp32_raw_fma_top(input, output); // 所有向量均通过独立HLS顶层。
    const std::uint32_t actual = output.to_uint();
    const std::uint32_t gold = isNan(expected) ? 0x7fc00000U : expected;
    ++checks;
    if(actual!=gold){
        if(failures<20){
            std::cerr << "[FAIL] " << label << std::hex
                      << " a=0x" << a << " b=0x" << b << " c=0x" << c
                      << " actual=0x" << actual << " expected=0x" << gold
                      << std::dec << std::endl;
        }
        ++failures;
    }
}

void check(const std::uint32_t a, const std::uint32_t b,
           const std::uint32_t c, const char* label){
    const auto expected = exactFma(a, b, c);
    checkExpected(a, b, c, expected, label);
#ifdef FSA_CHECK_HOST_FMA
    const auto host = toBits(std::fma(fromBits(a), fromBits(b), fromBits(c)));
    if(host!=expected && !(isNan(host) && isNan(expected))){
        if(failures<20){ std::cerr << "[FAIL] host/exact oracle disagreement" << std::endl; }
        ++failures;
    }
#endif
}

std::uint32_t randomBits(std::uint32_t& state){
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void testDirected(){
    struct Vector{ std::uint32_t a, b, c, expected; };
    const Vector vectors[] = {
        {0x3f800001U, 0x3f7ffffeU, 0xbf800000U, 0xa8800000U}, // fused残差
        {0x7f7fffffU, 0x40000000U, 0xff7fffffU, 0x7f7fffffU}, // 不提前溢出
        {0x00800000U, 0x33800000U, 0U, 0U}, // 半个最小非规格化，tie-even
        {1U, 0x3f000000U, 0U, 0U},
        {1U, 0x3fc00000U, 0U, 2U},
        {0x80000001U, 0x3f000000U, 0U, 0x80000000U},
        {0x00800000U, 0x3f800000U, 0x807fffffU, 1U},
        {0x3f800000U, 0x3f800000U, 0x33800000U, 0x3f800000U},
        {0x3f800001U, 0x3f800000U, 0x33800000U, 0x3f800002U},
        {0x3f800000U, 0x3f800000U, 0xbf800000U, 0U},
        {0x80000000U, 0x3f800000U, 0x80000000U, 0x80000000U},
        {0x80000000U, 0x3f800000U, 0U, 0U},
        {0x7f800000U, 0U, 0U, 0x7fc00000U},
        {0x7f800000U, 0x3f800000U, 0xff800000U, 0x7fc00000U},
        {0xff800000U, 0x3f800000U, 0xff800000U, 0xff800000U},
        {0x7f800001U, 0x3f800000U, 0U, 0x7fc00000U}
    };
    for(const auto& v : vectors){
        checkExpected(v.a, v.b, v.c, v.expected, "directed");
        if(exactFma(v.a, v.b, v.c)!=v.expected){
            std::cerr << "[FAIL] exact integer oracle self-check" << std::endl;
            ++failures;
        }
    }
}

void testSpecialGrid(){
    const std::uint32_t values[] = {
        0U, 0x80000000U, 1U, 0x80000001U, 0x007fffffU, 0x807fffffU,
        0x00800000U, 0x80800000U, 0x3f800000U, 0xbf800000U,
        0x7f7fffffU, 0xff7fffffU, 0x7f800000U, 0xff800000U,
        0x7fc12345U, 0xff800001U
    };
    for(const auto a : values){
        for(const auto b : values){
            for(const auto c : values){ check(a, b, c, "special grid"); }
        }
    }
}

void testRandom(){
    std::uint32_t state = 0x87153642U;
    for(int i=0; i<100000; ++i){
        const auto a = randomBits(state);
        const auto b = randomBits(state);
        const auto c = randomBits(state);
        check(a, b, c, "random");
    }
}

void testCancellation(){
    std::uint32_t state = 0x253fb901U;
    for(int i=0; i<20000; ++i){
        // 控制指数使乘积有限且非零，保留随机的全部23位小数。
        const auto a = ((80U+randomBits(state)%80U)<<23) |
            (randomBits(state)&0x807fffffU);
        const auto b = ((80U+randomBits(state)%80U)<<23) |
            (randomBits(state)&0x807fffffU);
        const auto rounded_product = exactFma(a, b, 0U);
        const auto magnitude = rounded_product&0x7fffffffU;
        for(int offset=-2; offset<=2; ++offset){
            const auto c = (std::uint32_t)((std::int64_t)magnitude+offset) |
                ((rounded_product^0x80000000U)&0x80000000U);
            check(a, b, c, "cancellation");
        }
    }
}

void testExponentBoundaries(){
    const unsigned exponents[] = {0, 1, 2, 22, 23, 24, 63, 126, 127, 128, 190, 230, 253, 254};
    const unsigned fractions[] = {0, 1, 0x003fffffU, 0x007fffffU};
    for(const auto ea : exponents){
        for(const auto eb : exponents){
            for(const auto fa : fractions){
                for(const auto fb : fractions){
                    const auto a = (ea<<23)|fa;
                    const auto b = (eb<<23)|fb;
                    for(const unsigned c : {0U, 1U, 0x007fffffU, 0x00800000U,
                                           0x3f800000U, 0xbf800000U, 0x7f7fffffU}){
                        check(a, b, c, "exponent boundary");
                        check(a^0x80000000U, b, c, "exponent boundary");
                    }
                }
            }
        }
    }
}

}  // namespace

int main(){
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
                  "binary32 host reference required");
    if(std::fesetround(FE_TONEAREST)!=0){
        std::cerr << "[FAIL] cannot select round-to-nearest-even" << std::endl;
        return 1;
    }
    testDirected();
    testSpecialGrid();
    testRandom();
    testCancellation();
    testExponentBoundaries();
    std::cout << (failures==0 ? "[PASS] " : "[FAIL] ")
              << "test_fp32_raw_fma_top: " << checks << " vectors, "
              << failures << " mismatches" << std::endl;
    return failures==0 ? 0 : 1;
}
