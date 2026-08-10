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

        static_assert(
            exp2PWLPieces == 8,
            "EXP2_PWL_INTERCEPT_BITS只适用于8段PWL"
        );

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

            // 截距位于(0.5, 1]，原指数只能是0x7e或0x7f
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

            const int integer_part = hls::trunc(x_acc);
            const acc_t fractional_part = x_acc - (acc_t)integer_part;

            exp2_counter_t piece =
                (exp2_counter_t)(hls::fabs(fractional_part)*(acc_t)exp2PWLPieces);

            return piece;
        }

    }  // namespace

    acc_t peMac(const elem_t in_a, const elem_t in_b, const acc_t in_c){
        return hls::fma((acc_t)in_a, (acc_t)in_b, in_c);
    }

    PeMacUnitOutput peMacUnit(const elem_t in_a, const elem_t in_b, 
                            const acc_t in_c, const bool in_exp2){
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
        const acc_t x_acc = (acc_t)x;
        const int integer_part = (int)hls::trunc(x_acc);
        const acc_t fractional_part = x_acc-(acc_t)integer_part;

        const acc_t intercept = restoreExp2PWLIntercept(encoded_intercept);

        const acc_t fractional_result = 
                        hls::fma(fractional_part, (acc_t)slope, intercept);

        return hls::ldexp(fractional_result, integer_part);
    }

    acc_t exp2PWLIntercept(const exp2_counter_t index){
        const ap_uint<32> bits = EXP2_PWL_INTERCEPT_BITS[index.to_uint()];
        const fp_struct<acc_t> view(bits);
        return view.to_ieee();
    }

    acc_t accExp2PWL(const acc_t x){
        // TODO：应使用PWL计算，可能需要构造查找表
        return std::exp2(x);
    }

    acc_t reciprocal(const acc_t value){
        // TODO: 应为多周期除法器，带有busy状态
        return (acc_t)1.0F/value;
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