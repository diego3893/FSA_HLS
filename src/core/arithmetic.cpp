#include "fsa/arithmetic.hpp"

#include <cmath>
#include <limits>
#include <utils/x_hls_utils.h>
#include <hls_math.h>

namespace fsa{

    namespace{

        /// @brief 把分段编号写入截距的[26:24]
        const ap_uint<32> EXP2_PWL_INTERCEPT_BITS[exp2PWLPieces] = {
            0x00800000,
            0x017e3c91,
            0x027b00a2,
            0x03768dcf,
            0x04711d65,
            0x056ae156,
            0x06640507,
            0x075cae0f
        };

        /**
         * @brief Accumulator内置PWL斜率的FP32位模式
         *
         * 与PE不同，Accumulator不会逐拍接收斜率和编码截距，而是根据
         * 输入的小数部分直接读取同一分段的斜率和普通FP32截距。
         */
        const ap_uint<32> ACC_EXP2_PWL_SLOPE_BITS[exp2PWLPieces] = {
            0x3f29f9c9,
            0x3f1bde51,
            0x3f0eee96,
            0x3f0311b7,
            0x3ef061c9,
            0x3edc6e66,
            0x3eca22e7,
            0x3eb95c1e
        };

        /// @brief Accumulator内置PWL截距的普通FP32位模式
        const ap_uint<32> ACC_EXP2_PWL_INTERCEPT_BITS[exp2PWLPieces] = {
            0x3f800000,
            0x3f7e3c91,
            0x3f7b00a2,
            0x3f768dcf,
            0x3f711d65,
            0x3f6ae156,
            0x3f640507,
            0x3f5cae0f
        };

        static_assert(
            exp2PWLPieces == 8,
            "EXP2_PWL_INTERCEPT_BITS只适用于8段PWL"
        );

        /**
         * @brief 使用IEEE符号位、阶码和尾数实现向零取整
         *
         * @param value FP32输入
         * @return int 向零取整结果
         */
        int truncToIntBits(const acc_t value){
            const fp_struct<acc_t> view(value);
            const ap_uint<32> bits = view.data();

            const bool sign = bits[31];
            const ap_uint<8> exponent_bits = bits.range(30, 23);
            const ap_uint<23> mantissa = bits.range(22, 0);

            const int exponent = (int)exponent_bits-127;

            // 零、非规格化数以及绝对值小于1的数，向零取整后都是0。
            if(exponent_bits==0 || exponent<0){
                return 0;
            }

            // NaN、Inf或超出int范围时进行饱和处理
            if(exponent_bits==0xff || exponent>=31){
                return sign ? (-2147483647-1) : 2147483647;
            }

            ap_uint<32> significand = 0;
            significand[23] = 1;
            significand.range(22, 0) = mantissa;

            ap_uint<32> magnitude;

            // 通过阶码恢复整数部分
            if(exponent >= 23){
                magnitude = significand << (exponent-23);
            }else{
                magnitude = significand >> (23-exponent);
            }

            const int result = (int)magnitude;

            return sign ? -result : result;
        }

        /**
         * @brief 计算value*2^exponent
         *
         * @param value 需要缩放的FP32数据
         * @param exponent 2的指数
         * @return acc_t value*2^exponent
         */
        acc_t ldexpByBits(const acc_t value, const int exponent){
            const fp_struct<acc_t> input_view(value);
            ap_uint<32> bits = input_view.data();

            const ap_uint<8> old_exponent_bits = bits.range(30, 23);
            const ap_uint<23> mantissa = bits.range(22, 0);

            if(old_exponent_bits == 0){
                return value;
            }

            // Inf和NaN
            if(old_exponent_bits == 0xff){
                return value;
            }

            const int new_exponent = (int)old_exponent_bits+exponent;

            // 把新阶码写回
            if(new_exponent>0 && new_exponent<255){
                bits.range(30, 23) = (ap_uint<8>)new_exponent;

                const fp_struct<acc_t> output_view(bits);
                return output_view.to_ieee();
            }

            // 上溢
            if(new_exponent >= 255){
                bits.range(30, 23) = (ap_uint<8>)0xff;
                bits.range(22, 0) = 0;

                const fp_struct<acc_t> output_view(bits);
                return output_view.to_ieee();
            }

            // 下溢，编码为非规格化数
            ap_uint<25> significand = 0;
            significand[23] = 1;
            significand.range(22, 0) = mantissa;

            const int right_shift = 1-new_exponent;

            // 太小了作0处理
            if(right_shift > 24){
                bits.range(30, 0) = 0;
                const fp_struct<acc_t> output_view(bits);
                return output_view.to_ieee();
            }

            ap_uint<25> shifted = significand >> right_shift;

            // 舍入RNE
            const ap_uint<25> mask = (((ap_uint<25>)1<<right_shift) - 1);

            // 通过掩码获取被移走的部分
            const ap_uint<25> remainder = significand & mask;
            // 舍入中点
            const ap_uint<25> halfway = (ap_uint<25>)1 << (right_shift-1);

            // 舍入
            const bool round_up =
                (remainder>halfway) || ((remainder==halfway) && shifted[0]);

            if(round_up){
                shifted = shifted+1;
            }

            // 舍入后可能刚好成为最小正常数
            if(shifted[23]){
                bits.range(30, 23) = (ap_uint<8>)1;
                bits.range(22, 0) = 0;
            }else{
                bits.range(30, 23) = 0;
                bits.range(22, 0) = shifted.range(22, 0);
            }

            const fp_struct<acc_t> output_view(bits);
            return output_view.to_ieee();
        }

        /**
         * @brief 从编码截距中读取分段编号
         * 
         * @param encoded_intercept 编码过后的截距
         * @return exp2_counter_t 解码后的分段编号
         */
        exp2_counter_t decodeExp2PWLIndex(const acc_t encoded_intercept){
            const fp_struct<acc_t> view(encoded_intercept);
            const ap_uint<32> bits = view.data();
            return bits.range(26, 24);
        }

        /**
         * @brief 恢复截距的阶码部分
         * 
         * @param encoded_intercept 编码过后的截距
         * @return acc_t 恢复后的FP32截距
         */
        acc_t restoreExp2PWLIntercept(const acc_t encoded_intercept){
            const fp_struct<acc_t> encoded_view(encoded_intercept);
            ap_uint<32> bits = encoded_view.data();

            // 阶码唯一有效位
            const ap_uint<1> exponent_lsb = bits[23];

            // 截距位于(0.5, 1]，原阶码只能是0x7e或0x7f
            // 恢复阶码
            bits.range(30, 23) = (ap_uint<8>)0x7e | (ap_uint<8>)exponent_lsb;

            const fp_struct<acc_t> restored_view(bits);
            return restored_view.to_ieee();
        }

        /**
         * @brief 计算x的小数部分属于哪个分段
         * 
         * @param x 完整指数
         * @return exp2_counter_t 分段编号 
         */
        exp2_counter_t exp2PWLPieceForX(const elem_t x){
            const acc_t x_acc = (acc_t)x;

            const int integer_part = truncToIntBits(x_acc);
            const acc_t fractional_part = x_acc - (acc_t)integer_part;

            exp2_counter_t piece =
                (exp2_counter_t)(hls::fabs(fractional_part)*(acc_t)exp2PWLPieces);

            return piece;
        }

        /**
         * @brief 把32位IEEE-754位模式解释为acc_t数值
         *
         * 这里是位视图转换，不是把无符号整数的数值转换为float。
         */
        acc_t accFloatFromBits(const ap_uint<32> bits){
            const fp_struct<acc_t> view(bits);
            return view.to_ieee();
        }

        /**
         * @brief 根据[-1, 0]内的小数部分选择Accumulator PWL分段
         *
         * 绝对值只用于计算分段编号；真正的FMA仍使用带符号小数部分。
         */
        unsigned int accExp2PieceForFraction(const acc_t fractional_part){
            const acc_t scaled_fraction =
                hls::fabs(fractional_part)*(acc_t)exp2PWLPieces;
            unsigned int index = static_cast<unsigned int>(scaled_fraction);

            // 防止浮点边界误差造成数组越界。
            if(index >= static_cast<unsigned int>(exp2PWLPieces)){
                index = static_cast<unsigned int>(exp2PWLPieces-1);
            }
            return index;
        }

    }  // namespace

    acc_t peMac(const elem_t in_a, const elem_t in_b, const acc_t in_c){
        return hls::fma((acc_t)in_a, (acc_t)in_b, in_c);
    }

    PeMacUnitOutput peMacUnit(const elem_t in_a, const elem_t in_b, 
                            const acc_t in_c, const bool in_exp2){
        #pragma HLS PIPELINE II=1
        PeMacUnitOutput output{};
        if(!in_exp2){
            output.out_accType = peMac(in_a, in_b, in_c);
            output.out_elemType = cvtAtoE(output.out_accType);
            output.out_exp2 = false;
            return output;
        }

        output.out_accType = peExp2PWL(in_a, in_b, in_c);
        output.out_elemType = cvtAtoE(output.out_accType);

        const exp2_counter_t intercept_index = decodeExp2PWLIndex(in_c);

        output.out_exp2 = intercept_index==exp2PWLPieceForX(in_a);

        return output;
    }

    acc_t accUnit(const acc_t in_a, const acc_t in_b, const acc_t in_c){
        return hls::fma(in_a, in_b, in_c);
    }

    CmpUnitOutput accCmp(const acc_t in_a, const acc_t in_b){
        CmpUnitOutput output{};
        output.out_diff = hls::fma(in_a, (acc_t)1.0F, -in_b);
        const fp_struct<acc_t> diff_view(output.out_diff);
        output.out_max = diff_view.sign[0] ? in_b : in_a;
        return output;
    }

    elem_t cvtAtoE(const acc_t a){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=2 max=2
        return (elem_t)a;
    }

    acc_t viewEasA(const elem_t e){
        const fp_struct<elem_t> elem_view(e);
        ap_uint<32> acc_bits = 0;
        acc_bits.range(15, 0) = elem_view.data();
        const fp_struct<acc_t> acc_view(acc_bits);
        return acc_view.to_ieee();
    }

    elem_t viewAasE(const acc_t a){
        const fp_struct<acc_t> acc_view(a);
        ap_uint<16> elem_bits = acc_view.data().range(15, 0);
        const fp_struct<elem_t> elem_view(elem_bits);
        return elem_view.to_ieee();
    }

    acc_t peExp2PWL(const elem_t x, const elem_t slope, 
                    const acc_t encoded_intercept){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=12 max=12
        const acc_t x_acc = (acc_t)x;
        const int integer_part = truncToIntBits(x_acc);
        const acc_t fractional_part = x_acc-(acc_t)integer_part;

        const acc_t intercept = restoreExp2PWLIntercept(encoded_intercept);

        const acc_t fractional_result = 
                        hls::fma(fractional_part, (acc_t)slope, intercept);

        return ldexpByBits(fractional_result, integer_part);
    }

    acc_t exp2PWLIntercept(const exp2_counter_t index){
        const ap_uint<32> bits = EXP2_PWL_INTERCEPT_BITS[index.to_uint()];
        const fp_struct<acc_t> view(bits);
        return view.to_ieee();
    }

    acc_t accExp2PWL(const acc_t x){
        const int integer_part = static_cast<int>(hls::trunc(x));
        const acc_t fractional_part = x-static_cast<acc_t>(integer_part);
        const unsigned int index = accExp2PieceForFraction(fractional_part);

        const acc_t slope =
            accFloatFromBits(ACC_EXP2_PWL_SLOPE_BITS[index]);
        const acc_t intercept =
            accFloatFromBits(ACC_EXP2_PWL_INTERCEPT_BITS[index]);

        const acc_t fractional_result =
            hls::fma(fractional_part, slope, intercept);
        return hls::ldexp(fractional_result, integer_part);
    }

    elem_t elemZero(){
        return (elem_t)0.0F;
    }

    elem_t elemOne(){
        return (elem_t)1.0F;
    }

    acc_t accZero(){
        return (acc_t)0.0F;
    }

    acc_t accMinimum(){
        return -std::numeric_limits<acc_t>::infinity();
    }

    acc_t attentionScale(){
        return (acc_t)0.7213475204F;
    }
}  // namespace fsa