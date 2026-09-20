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

    int highestBit22(const ap_uint<22> value){
        #pragma HLS INLINE
        int highest = -1;
        for(int bit=21; bit>=0; --bit){
            #pragma HLS UNROLL
            if(highest<0 && value[bit]){
                highest = bit;
            }
        }
        return highest;
    }

    int highestBit24(const ap_uint<24> value){
        #pragma HLS INLINE
        int highest = -1;
        for(int bit=23; bit>=0; --bit){
            #pragma HLS UNROLL
            if(highest<0 && value[bit]){
                highest = bit;
            }
        }
        return highest;
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
        // {value, 27'b0}右移后，高27位是商，低27位保存所有丢弃位。
        // 用丢弃位归约生成sticky，去掉动态掩码减一的进位链。
        // 上面的边界判断保证distance在1..26内，窄化不会截断有效移位量。
        ap_uint<54> extended = 0;
        extended.range(53, 27) = value;
        const ap_uint<5> distance = shift;
        const ap_uint<54> shifted = extended >> distance;
        ap_uint<27> result = shifted.range(53, 27);
        result[0] = result[0] || (shifted.range(26, 0)!=0);
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

        const int product_highest = highestBit22(product);
        if(c.zero){
            result.sign = product_sign;
            result.zero = false;
            result.top_exponent =
                a.lsb_exponent+b.lsb_exponent+product_highest;
            result.significand =
                (ap_uint<27>)product << (26-product_highest);
            return result;
        }

        const int c_highest = highestBit24(c.significand);
        const int product_top =
            a.lsb_exponent+b.lsb_exponent+product_highest;
        const int c_top = c.lsb_exponent+c_highest;
        const int common_top = product_top>c_top ? product_top : c_top;

        ap_uint<27> product_aligned =
            (ap_uint<27>)product << (26-product_highest);
        ap_uint<27> c_aligned =
            (ap_uint<27>)c.significand << (26-c_highest);
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

            const int piece_shift = binary_scale+3;
            ap_uint<14> piece_value = piece_shift>=0
                ? (ap_uint<14>)remainder << piece_shift
                : (ap_uint<14>)remainder >> (-piece_shift);
            if(piece_value>=8){
                piece_value = 7;
            }
            prepared.piece = piece_value.range(2, 0);
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
        // 拼接低24位零后一次右移，同时得到商、guard和其余丢弃位。
        // 避免动态mask减一、halfway移位及宽余数比较串到输出路径。
        const ap_uint<5> distance = right_shift;
        ap_uint<48> extended = 0;
        extended.range(47, 24) = significand;
        const ap_uint<48> aligned = extended >> distance;
        ap_uint<25> shifted = aligned.range(47, 24);
        const bool guard = aligned[23];
        const bool sticky = aligned.range(22, 0)!=0;
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
