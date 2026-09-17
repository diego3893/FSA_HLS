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
        ap_uint<16> fractional_bits = 0;
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

    int highestBit27(const ap_uint<27> value){
        #pragma HLS INLINE
        int highest = -1;
        for(int bit=26; bit>=0; --bit){
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
        ap_uint<27> shifted = value >> shift;
        const ap_uint<27> mask =
            (((ap_uint<27>)1 << shift)-(ap_uint<27>)1);
        if((value & mask)!=0){
            shifted[0] = 1;
        }
        return shifted;
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
            const int highest = highestBit27(low);
            const int left_shift = 26-highest;
            result.top_exponent = common_top-left_shift;
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
            if(exponent==15 && mantissa>0x7fe000){
                result.range(14, 10) = 0x1f;
                return result;
            }
            result.range(14, 10) = (ap_uint<5>)(exponent+15);
            result.range(9, 0) = mantissa.range(22, 13);
            return result;
        }
        // 当前工程的half转换模型把FP16非规格化输出冲刷为正零。
        result[15] = false;
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

        int highest = -1;
        for(int bit=10; bit>=0; --bit){
            #pragma HLS UNROLL
            if(highest<0 && remainder[bit]){
                highest = bit;
            }
        }
        if(highest>=0){
            ap_uint<16> fractional = 0;
            fractional[15] = sign;
            const int unbiased = highest+binary_scale;
            if(unbiased>=-14){
                fractional.range(14, 10) =
                    (ap_uint<5>)(unbiased+15);
                const ap_uint<11> normalized =
                    remainder << (10-highest);
                fractional.range(9, 0) = normalized.range(9, 0);
            }else{
                const int subnormal_shift = binary_scale+24;
                fractional.range(9, 0) = subnormal_shift>=0
                    ? (ap_uint<10>)(remainder << subnormal_shift)
                    : (ap_uint<10>)(remainder >> (-subnormal_shift));
            }
            prepared.fractional_bits = fractional;

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

        ap_uint<25> significand = 0;
        significand[23] = 1;
        significand.range(22, 0) = mantissa;
        const int right_shift = 1-new_exponent;
        if(right_shift>24){
            bits.range(30, 0) = 0;
            return bits;
        }
        ap_uint<25> shifted = significand >> right_shift;
        const ap_uint<25> mask =
            (((ap_uint<25>)1 << right_shift)-(ap_uint<25>)1);
        const ap_uint<25> remainder = significand & mask;
        const ap_uint<25> halfway =
            (ap_uint<25>)1 << (right_shift-1);
        const bool round_up = remainder>halfway ||
            (remainder==halfway && shifted[0]);
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
        const ap_uint<16> a_bits,
        const ap_uint<16> b_bits,
        const ap_uint<32> c_bits
    ){
        #pragma HLS INLINE
        const HalfFields a = unpackHalf(a_bits);
        const HalfFields b = unpackHalf(b_bits);
        const FloatFields c = unpackFloat(c_bits);
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
        const ap_uint<16> operand_a = input.in_exp2
            ? prepared.fractional_bits : input.in_a_bits;
        const ap_uint<32> operand_c = input.in_exp2
            ? restoreEncodedIntercept(input.in_c_bits)
            : input.in_c_bits;

        ap_uint<32> result_bits = finiteRawFma(
            operand_a, input.in_b_bits, operand_c
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
