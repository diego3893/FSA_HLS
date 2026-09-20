#include "fsa/stream/fp32_raw_fma.hpp"

namespace fsa{
namespace{

    struct Fp32Fields{
        bool sign = false;
        bool zero = true;
        bool infinity = false;
        bool nan = false;
        ap_uint<24> significand = 0;
        int lsb_exponent = -149;
    };

    Fp32Fields unpackFp32(const ap_uint<32> bits){
        #pragma HLS INLINE
        Fp32Fields value{};
        value.sign = bits[31];
        const ap_uint<8> exponent = bits.range(30, 23);
        const ap_uint<23> fraction = bits.range(22, 0);
        value.zero = exponent==0 && fraction==0;
        value.infinity = exponent==255 && fraction==0;
        value.nan = exponent==255 && fraction!=0;
        value.significand = fraction;
        if(exponent!=0){
            value.significand[23] = 1;
            value.lsb_exponent = (int)exponent-150;
        }
        return value;
    }

    ap_uint<32> signedZero(const bool sign){
        #pragma HLS INLINE
        ap_uint<32> bits = 0;
        bits[31] = sign;
        return bits;
    }

    ap_uint<32> signedInfinity(const bool sign){
        #pragma HLS INLINE
        ap_uint<32> bits = signedZero(sign);
        bits.range(30, 23) = 255;
        return bits;
    }

    ap_uint<51> shiftRightJam51(
        const ap_uint<51> value, const int shift
    ){
        #pragma HLS INLINE
        if(shift<=0){
            return value;
        }
        if(shift>=51){
            return value!=0 ? (ap_uint<51>)1 : (ap_uint<51>)0;
        }
        // 同时取得商和所有丢弃位，避免动态mask减一；不丢sticky。
        ap_uint<102> extended = 0;
        extended.range(101, 51) = value;
        const ap_uint<6> distance = shift;
        const ap_uint<102> shifted = extended >> distance;
        ap_uint<51> result = shifted.range(101, 51);
        result[0] = result[0] || (shifted.range(50, 0)!=0);
        return result;
    }

    ap_uint<32> roundPackFp32(
        const bool sign, int top_exponent, ap_uint<51> significand
    ){
        #pragma HLS INLINE
        // bit50为隐藏位。保留完整乘积到这里才统一舍入到24位，
        // 不先舍入乘积，也不先舍入正常数再转非规格化数。
        if(top_exponent < -126){
            significand = shiftRightJam51(significand, -126-top_exponent);
            top_exponent = -126;
        }
        ap_uint<25> rounded = significand.range(50, 27);
        const bool guard = significand[26];
        const bool sticky = significand.range(25, 0)!=0;
        if(guard && (sticky || rounded[0])){
            rounded = rounded+1;
        }
        if(rounded[24]){
            rounded = rounded >> 1;
            ++top_exponent;
        }
        if(top_exponent>127){
            return signedInfinity(sign);
        }
        ap_uint<32> bits = signedZero(sign);
        // 下溢舍入也可能恰好进位到最小正常数。
        if(rounded[23]){
            bits.range(30, 23) = (ap_uint<8>)(top_exponent+127);
        }
        bits.range(22, 0) = rounded.range(22, 0);
        return bits;
    }

    ap_uint<32> finiteFp32Fma(
        const Fp32Fields& a, const Fp32Fields& b, const Fp32Fields& c
    ){
        #pragma HLS INLINE
        // 仅此一个乘法表达。一个FP32尾数乘法可能映射为多个DSP，
        // DSP数量和流水拍数由独立综合核验，不等同于复制FMA。
        ap_uint<48> product = a.significand*b.significand;
        #pragma HLS BIND_OP variable=product op=mul impl=dsp latency=2
        const bool product_sign = a.sign^b.sign;
        const ap_uint<6> product_clz = product.countLeadingZeros();
        const int product_top = a.lsb_exponent+b.lsb_exponent+
            47-(int)product_clz;
        const ap_uint<48> product_normalized = product << product_clz;
        ap_uint<51> product_aligned = 0;
        product_aligned.range(50, 3) = product_normalized;

        if(c.zero){
            return roundPackFp32(product_sign, product_top, product_aligned);
        }

        const ap_uint<5> c_clz = c.significand.countLeadingZeros();
        const int c_top = c.lsb_exponent+23-(int)c_clz;
        const ap_uint<24> c_normalized = c.significand << c_clz;
        ap_uint<51> c_aligned = 0;
        c_aligned.range(50, 27) = c_normalized;
        const int common_top = product_top>c_top ? product_top : c_top;
        product_aligned = shiftRightJam51(product_aligned, common_top-product_top);
        c_aligned = shiftRightJam51(c_aligned, common_top-c_top);

        // 48位乘积加3个低位：接近相消时对齐仍然精确；远距对齐
        // 才将尾部压成sticky，且保留最终24位舍入所需的全部信息。
        ap_uint<52> magnitude = 0;
        bool sign = product_sign;
        if(product_sign==c.sign){
            magnitude = (ap_uint<52>)product_aligned+c_aligned;
        }else if(product_aligned>=c_aligned){
            magnitude = (ap_uint<52>)product_aligned-c_aligned;
        }else{
            magnitude = (ap_uint<52>)c_aligned-product_aligned;
            sign = c.sign;
        }
        if(magnitude==0){
            return signedZero(false); // RNE下精确相消为+0。
        }

        int top_exponent = common_top;
        ap_uint<51> significand = 0;
        if(magnitude[51]){
            ++top_exponent;
            significand = magnitude.range(51, 1);
            significand[0] = significand[0] || magnitude[0];
        }else{
            const ap_uint<51> low = magnitude.range(50, 0);
            const ap_uint<6> distance = low.countLeadingZeros();
            top_exponent -= (int)distance;
            significand = low << distance;
        }
        return roundPackFp32(sign, top_exponent, significand);
    }

}  // namespace

    ap_uint<32> fp32RawFma(const Fp32RawFmaInput& input){
        #pragma HLS INLINE
        const Fp32Fields a = unpackFp32(input.a_bits);
        const Fp32Fields b = unpackFp32(input.b_bits);
        const Fp32Fields c = unpackFp32(input.c_bits);
        const bool product_sign = a.sign^b.sign;
        const bool product_infinity = a.infinity || b.infinity;
        if(a.nan || b.nan || c.nan ||
                (a.infinity && b.zero) || (b.infinity && a.zero) ||
                (product_infinity && c.infinity && product_sign!=c.sign)){
            return (ap_uint<32>)0x7fc00000U;
        }
        if(product_infinity){
            return signedInfinity(product_sign);
        }
        if(c.infinity){
            return input.c_bits;
        }
        if(a.zero || b.zero){
            if(c.zero){
                return signedZero(product_sign==c.sign ? product_sign : false);
            }
            return input.c_bits;
        }
        return finiteFp32Fma(a, b, c);
    }

}  // namespace fsa
