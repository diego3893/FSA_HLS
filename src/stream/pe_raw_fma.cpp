#include "fsa/stream/pe_raw_fma.hpp"

namespace fsa{
namespace{

    constexpr std::uint32_t FP32_CANONICAL_QNAN = 0x7fc00000U;

    struct HalfFields{
        bool sign = false;
        bool zero = true;
        bool infinity = false;
        bool nan = false;
        ap_uint<11> significand = 0;
        int lsb_exponent = -24;
    };

    struct FloatFields{
        bool sign = false;
        bool zero = true;
        bool infinity = false;
        bool nan = false;
        ap_uint<24> significand = 0;
        int lsb_exponent = -149;
    };

    struct NormalizedSum{
        bool sign = false;
        bool zero = true;
        int top_exponent = 0;
        // bit26是隐藏位，bit2/1/0分别为guard/round/sticky。
        ap_uint<27> significand = 0;
    };

    struct Exp2Prepared{
        // 小数部分直接以精确的significand*2^lsb_exponent传递，
        // 不重新打包成FP16，也不在DSP输入路径增加规格化移位。
        HalfFields fractional{};
        int integer = 0;
        ap_uint<3> piece = 0;
        bool force_zero = false;
    };

    HalfFields unpackHalf(const ap_uint<16> bits){
        #pragma HLS INLINE
        HalfFields fields{};
        fields.sign = bits[15];
        const ap_uint<5> exponent = bits.range(14, 10);
        const ap_uint<10> mantissa = bits.range(9, 0);
        fields.nan = exponent==0x1f && mantissa!=0;
        fields.infinity = exponent==0x1f && mantissa==0;
        fields.zero = exponent==0 && mantissa==0;
        if(fields.zero || fields.infinity || fields.nan){
            return fields;
        }
        if(exponent==0){
            fields.significand = mantissa;
            fields.lsb_exponent = -24;
        }else{
            fields.significand = ((ap_uint<11>)1 << 10) | mantissa;
            fields.lsb_exponent = (int)exponent-25;
        }
        return fields;
    }

    FloatFields unpackFloat(const ap_uint<32> bits){
        #pragma HLS INLINE
        FloatFields fields{};
        fields.sign = bits[31];
        const ap_uint<8> exponent = bits.range(30, 23);
        const ap_uint<23> mantissa = bits.range(22, 0);
        fields.nan = exponent==0xff && mantissa!=0;
        fields.infinity = exponent==0xff && mantissa==0;
        fields.zero = exponent==0 && mantissa==0;
        if(fields.zero || fields.infinity || fields.nan){
            return fields;
        }
        if(exponent==0){
            fields.significand = mantissa;
            fields.lsb_exponent = -149;
        }else{
            fields.significand = ((ap_uint<24>)1 << 23) | mantissa;
            fields.lsb_exponent = (int)exponent-150;
        }
        return fields;
    }

    ap_uint<27> shiftRightJam27(
        const ap_uint<27> value,
        const int shift
    ){
        #pragma HLS INLINE
        if(shift<=0){
            return value;
        }
        if(shift>=27){
            return value!=0 ? (ap_uint<27>)1 : (ap_uint<27>)0;
        }
        // 每级固定移位只保留27位商；丢弃位持续汇入sticky。
        // distance在1..26内，五级16/8/4/2/1覆盖全部有效移位量。
        const ap_uint<5> distance = shift;
        ap_uint<27> result = value;
        bool sticky = false;
        if(distance[4]){
            sticky = sticky || result.range(15, 0)!=0;
            result = result >> 16;
        }
        if(distance[3]){
            sticky = sticky || result.range(7, 0)!=0;
            result = result >> 8;
        }
        if(distance[2]){
            sticky = sticky || result.range(3, 0)!=0;
            result = result >> 4;
        }
        if(distance[1]){
            sticky = sticky || result.range(1, 0)!=0;
            result = result >> 2;
        }
        if(distance[0]){
            sticky = sticky || result[0];
            result = result >> 1;
        }
        result[0] = result[0] || sticky;
        return result;
    }

    ap_uint<32> makeInfinity32(const bool sign){
        #pragma HLS INLINE
        ap_uint<32> bits = 0;
        bits[31] = sign;
        bits.range(30, 23) = 0xff;
        return bits;
    }

    NormalizedSum addFiniteProduct(
        const HalfFields& a,
        const HalfFields& b,
        const FloatFields& c
    ){
        #pragma HLS INLINE
        NormalizedSum result{};
        const bool product_sign = a.sign^b.sign;

        ap_uint<22> product =
            (ap_uint<22>)a.significand*(ap_uint<22>)b.significand;
        #pragma HLS BIND_OP variable=product op=mul impl=dsp latency=1

        // finiteRawFma只在乘积非零时进入这里。直接用前导零数同时生成
        // 乘积最高位指数和规格化左移量，避免DSP后串联22位优先编码器。
        const ap_uint<5> product_left_shift = product.countLeadingZeros();
        const int product_top =
            a.lsb_exponent+b.lsb_exponent+21-(int)product_left_shift;
        const ap_uint<27> product_normalized =
            (ap_uint<27>)product << (5+(int)product_left_shift);
        if(c.zero){
            result.sign = product_sign;
            result.zero = false;
            result.top_exponent = product_top;
            result.significand = product_normalized;
            return result;
        }

        // 此处c非零；直接以前导零数生成最高位指数与规格化移位量。
        const ap_uint<5> c_left_shift = c.significand.countLeadingZeros();
        const int c_top = c.lsb_exponent+23-(int)c_left_shift;
        const int common_top = product_top>c_top ? product_top : c_top;

        ap_uint<27> product_aligned = product_normalized;
        ap_uint<27> c_aligned =
            (ap_uint<27>)c.significand << (3+(int)c_left_shift);
        product_aligned = shiftRightJam27(
            product_aligned, common_top-product_top
        );
        c_aligned = shiftRightJam27(c_aligned, common_top-c_top);

        ap_uint<28> magnitude = 0;
        if(product_sign==c.sign){
            magnitude = (ap_uint<28>)product_aligned+c_aligned;
            result.sign = product_sign;
        }else if(product_aligned>=c_aligned){
            magnitude = (ap_uint<28>)product_aligned-c_aligned;
            result.sign = product_sign;
        }else{
            magnitude = (ap_uint<28>)c_aligned-product_aligned;
            result.sign = c.sign;
        }

        if(magnitude==0){
            result.zero = true;
            result.sign = false;
            return result;
        }

        result.zero = false;
        if(magnitude[27]){
            result.top_exponent = common_top+1;
            result.significand = magnitude.range(27, 1);
            result.significand[0] =
                result.significand[0] || magnitude[0];
        }else{
            const ap_uint<27> low = magnitude.range(26, 0);
            // 此分支的low非零，27位前导零数就是规格化所需的0..26位移。
            // 直接使用HLS的ctlz，避免最高位编码后再串联26-highest减法。
            const ap_uint<5> left_shift = low.countLeadingZeros();
            result.top_exponent = common_top-(int)left_shift;
            result.significand = low << left_shift;
        }
        return result;
    }

    ap_uint<32> roundPackFloat32(const NormalizedSum& value){
        #pragma HLS INLINE
        if(value.zero){
            ap_uint<32> zero = 0;
            zero[31] = value.sign;
            return zero;
        }

        int exponent = value.top_exponent;
        ap_uint<27> significand = value.significand;
        bool subnormal = false;
        if(exponent < -126){
            significand = shiftRightJam27(
                significand, -126-exponent
            );
            exponent = -126;
            subnormal = true;
        }

        ap_uint<25> rounded = (ap_uint<25>)(significand >> 3);
        const bool round_up = significand[2] &&
            (significand[1] || significand[0] || rounded[0]);
        if(round_up){
            rounded = rounded+1;
        }

        ap_uint<32> bits = 0;
        bits[31] = value.sign;
        if(subnormal){
            if(rounded[23]){
                bits.range(30, 23) = 1;
                bits.range(22, 0) = 0;
            }else{
                bits.range(30, 23) = 0;
                bits.range(22, 0) = rounded.range(22, 0);
            }
            return bits;
        }

        if(rounded[24]){
            rounded = rounded >> 1;
            ++exponent;
        }
        if(exponent>127){
            return makeInfinity32(value.sign);
        }
        bits.range(30, 23) = (ap_uint<8>)(exponent+127);
        bits.range(22, 0) = rounded.range(22, 0);
        return bits;
    }

    ap_uint<16> float32ToHalfBits(const ap_uint<32> bits){
        #pragma HLS INLINE
        const bool sign = bits[31];
        const ap_uint<8> exponent_bits = bits.range(30, 23);
        const ap_uint<23> mantissa = bits.range(22, 0);
        ap_uint<16> result = 0;
        result[15] = sign;

        if(exponent_bits==0xff){
            result.range(14, 10) = 0x1f;
            if(mantissa!=0){
                result.range(9, 0) = 0x3ff;
            }
            return result;
        }
        if(exponent_bits==0){
            return result;
        }

        const int exponent = (int)exponent_bits-127;
        if(exponent>15){
            result.range(14, 10) = 0x1f;
            return result;
        }

        if(exponent>=-14){
            ap_uint<11> rounded = mantissa >> 13;
            const ap_uint<13> remainder = mantissa.range(12, 0);
            const bool round_up = remainder>0x1000 ||
                (remainder==0x1000 && rounded[0]);
            if(round_up){
                rounded = rounded+1;
            }

            int half_exponent = exponent+15;
            if(rounded[10]){
                rounded = 0;
                ++half_exponent;
            }
            if(half_exponent>=0x1f){
                result.range(14, 10) = 0x1f;
                return result;
            }
            result.range(14, 10) = (ap_uint<5>)half_exponent;
            result.range(9, 0) = rounded.range(9, 0);
            return result;
        }
        // Vitis half转换把FP16非规格化输出冲刷为带符号零。
        return result;
    }

    Exp2Prepared prepareExp2(const ap_uint<16> x_bits){
        #pragma HLS INLINE
        Exp2Prepared prepared{};
        const bool sign = x_bits[15];
        const ap_uint<5> exponent_bits = x_bits.range(14, 10);
        const ap_uint<10> mantissa = x_bits.range(9, 0);
        if(exponent_bits==0x1f){
            prepared.force_zero = sign && mantissa==0;
            return prepared;
        }

        ap_uint<11> significand = mantissa;
        int binary_scale = -24;
        if(exponent_bits!=0){
            significand[10] = 1;
            binary_scale = (int)exponent_bits-25;
        }

        ap_uint<16> integer_magnitude = 0;
        ap_uint<11> remainder = 0;
        if(binary_scale>=0){
            integer_magnitude =
                (ap_uint<16>)significand << binary_scale;
        }else{
            const int right_shift = -binary_scale;
            if(right_shift>=11){
                remainder = significand;
            }else{
                integer_magnitude = significand >> right_shift;
                const ap_uint<11> mask =
                    ((ap_uint<11>)1 << right_shift)-1;
                remainder = significand & mask;
            }
        }
        prepared.integer = sign
            ? -(int)integer_magnitude : (int)integer_magnitude;

        if(remainder!=0){
            prepared.fractional.sign = sign;
            prepared.fractional.zero = false;
            prepared.fractional.significand = remainder;
            prepared.fractional.lsb_exponent = binary_scale;

            // 余数严格小于一个整数单位，乘8后的整数部分只有3位。
            const int fractional_bits = -binary_scale;
            if(fractional_bits > 3){
                prepared.piece = (ap_uint<3>)(remainder >> (fractional_bits-3));
            }else{
                prepared.piece = (ap_uint<3>)(remainder << (3-fractional_bits));
            }
        }
        return prepared;
    }

    ap_uint<32> restoreEncodedIntercept(ap_uint<32> bits){
        #pragma HLS INLINE
        const ap_uint<1> exponent_lsb = bits[23];
        bits.range(30, 23) = (ap_uint<8>)0x7e | exponent_lsb;
        return bits;
    }

    ap_uint<32> scaleFloatByPowerOfTwo(
        ap_uint<32> bits,
        const int exponent
    ){
        #pragma HLS INLINE
        const ap_uint<8> old_exponent = bits.range(30, 23);
        const ap_uint<23> mantissa = bits.range(22, 0);
        if(old_exponent==0 || old_exponent==0xff){
            return bits;
        }

        const int new_exponent = (int)old_exponent+exponent;
        if(new_exponent>0 && new_exponent<255){
            bits.range(30, 23) = (ap_uint<8>)new_exponent;
            return bits;
        }
        if(new_exponent>=255){
            bits.range(30, 23) = 0xff;
            bits.range(22, 0) = 0;
            return bits;
        }

        ap_uint<24> significand = 0;
        significand[23] = 1;
        significand.range(22, 0) = mantissa;
        const int right_shift = 1-new_exponent;
        if(right_shift>24){
            bits.range(30, 0) = 0;
            return bits;
        }
        // 此分支的new_exponent<=0；上面的边界检查保证移位量在1..24。
        // 24位有效数后接1位guard，逐级固定右移并归约丢弃位。
        // 高24位是商、最低位是guard，sticky记录更低的所有位。
        const ap_uint<5> distance = right_shift;
        ap_uint<25> aligned = (ap_uint<25>)significand << 1;
        bool sticky = false;
        if(distance[4]){
            sticky = sticky || aligned.range(15, 0)!=0;
            aligned = aligned >> 16;
        }
        if(distance[3]){
            sticky = sticky || aligned.range(7, 0)!=0;
            aligned = aligned >> 8;
        }
        if(distance[2]){
            sticky = sticky || aligned.range(3, 0)!=0;
            aligned = aligned >> 4;
        }
        if(distance[1]){
            sticky = sticky || aligned.range(1, 0)!=0;
            aligned = aligned >> 2;
        }
        if(distance[0]){
            sticky = sticky || aligned[0];
            aligned = aligned >> 1;
        }
        ap_uint<25> shifted = aligned.range(24, 1);
        const bool guard = aligned[0];
        const bool round_up = guard && (sticky || shifted[0]);
        if(round_up){
            shifted = shifted+1;
        }
        if(shifted[23]){
            bits.range(30, 23) = 1;
            bits.range(22, 0) = 0;
        }else{
            bits.range(30, 23) = 0;
            bits.range(22, 0) = shifted.range(22, 0);
        }
        return bits;
    }

    ap_uint<32> finiteRawFma(
        const HalfFields& a,
        const HalfFields& b,
        const FloatFields& c,
        const ap_uint<32> c_bits
    ){
        #pragma HLS INLINE
        const bool product_sign = a.sign^b.sign;

        if(a.nan || b.nan || c.nan){
            return FP32_CANONICAL_QNAN;
        }
        if((a.infinity && b.zero) || (b.infinity && a.zero)){
            return FP32_CANONICAL_QNAN;
        }
        const bool product_infinity = a.infinity || b.infinity;
        if(product_infinity && c.infinity && product_sign!=c.sign){
            return FP32_CANONICAL_QNAN;
        }
        if(product_infinity){
            return makeInfinity32(product_sign);
        }
        if(c.infinity){
            return c_bits;
        }

        const bool product_zero = a.zero || b.zero;
        if(product_zero && c.zero){
            ap_uint<32> zero = 0;
            zero[31] = product_sign==c.sign ? product_sign : false;
            return zero;
        }
        if(product_zero){
            return c_bits;
        }

        return roundPackFloat32(addFiniteProduct(a, b, c));
    }

}  // namespace

    PeRawFmaOutput peRawFma(const PeRawFmaInput& input){
        #pragma HLS INLINE
        const Exp2Prepared prepared = prepareExp2(input.in_a_bits);
        HalfFields operand_a = unpackHalf(input.in_a_bits);
        if(input.in_exp2){
            operand_a = prepared.fractional;
        }
        const HalfFields operand_b = unpackHalf(input.in_b_bits);
        const ap_uint<32> operand_c_bits = input.in_exp2
            ? restoreEncodedIntercept(input.in_c_bits)
            : input.in_c_bits;
        const FloatFields operand_c = unpackFloat(operand_c_bits);

        ap_uint<32> result_bits = finiteRawFma(
            operand_a, operand_b, operand_c, operand_c_bits
        );
        if(input.in_exp2){
            result_bits = scaleFloatByPowerOfTwo(
                result_bits, prepared.integer
            );
            if(prepared.force_zero){
                result_bits = 0;
            }
        }

        PeRawFmaOutput output{};
        output.out_acc_bits = result_bits;
        output.out_elem_bits = float32ToHalfBits(result_bits);
        output.out_exp2 = input.in_exp2 &&
            input.in_c_bits.range(26, 24)==prepared.piece;
        return output;
    }

}  // namespace fsa
