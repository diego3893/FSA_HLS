#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{

    namespace{

        /// @brief 单拍恢复除法器的输出；valid只在DONE阶段置位
        struct ReciprocalTickOutput{
            bool valid = false;
            acc_t value = 0.0F;
        };

        /**
         * @brief 把IEEE-754 FP32位模式解释为acc_t
         *
         * 这是位视图转换，不是整数到浮点数的数值转换。
         */
        acc_t acc_float_from_bits(const ap_uint<32> bits){
            const fp_struct<acc_t> view(bits);
            return view.to_ieee();
        }

        /**
         * @brief 恢复除法的一位商计算
         *
         * 进入函数时remainder保存当前部分余数。先尝试减去divisor并
         * 产生一位商，再把余数左移，为下一位商准备数据。
         */
        void div_step(
            ap_uint<25>& remainder,
            ap_uint<26>& quotient,
            const ap_uint<24> divisor
        ){
            #pragma HLS INLINE

            quotient <<= 1;
            if(remainder >= (ap_uint<25>)divisor){
                remainder -= (ap_uint<25>)divisor;
                quotient[0] = 1;
            }
            remainder <<= 1;
        }

        /**
         * @brief 接收一个新的1.0/denominator请求并初始化除法状态
         *
         * 普通数被拆成24位规格化有效数和无偏指数。zero、Inf、NaN
         * 也保持同样的13个ITER周期，只在DONE时选择预先生成的结果，
         * 从而让所有输入具有固定reciprocalLatency。
         */
        void begin_reciprocal(
            ReciprocalDividerState& state,
            const acc_t denominator
        ){
            state = ReciprocalDividerState{};
            state.phase = ReciprocalPhase::ITER;

            const fp_struct<acc_t> view(denominator);
            const ap_uint<32> bits = view.data();
            const ap_uint<1> sign = bits[31];
            const ap_uint<8> exponent = bits.range(30, 23);
            const ap_uint<23> fraction = bits.range(22, 0);

            state.result_sign = sign[0];

            // NaN和Infinity。
            if(exponent == (ap_uint<8>)0xff){
                state.special = true;
                if(fraction != 0){
                    // 使用固定quiet NaN，避免把signaling NaN继续传播。
                    state.special_result_bits = (ap_uint<32>)0x7fc00000;
                }else{
                    // 1/(+/-Inf) = +/-0。
                    state.special_result_bits = ((ap_uint<32>)sign) << 31;
                }
                return;
            }

            // 正零或负零。
            if(exponent == 0 && fraction == 0){
                state.special = true;
                // 1/(+/-0) = +/-Inf。
                state.special_result_bits =
                    (((ap_uint<32>)sign) << 31) | (ap_uint<32>)0x7f800000;
                return;
            }

            ap_uint<24> normalized_significand = 0;
            int denominator_exponent = 0;

            if(exponent == 0){
                // 次正规数没有隐藏位。找最高的1，并左移成1.x格式。
                int leading_one = 0;
                bool found = false;
                for(int bit=22; bit>=0; --bit){
                    #pragma HLS UNROLL
                    if(!found && fraction[bit]){
                        leading_one = bit;
                        found = true;
                    }
                }

                normalized_significand =
                    (ap_uint<24>)fraction << (23-leading_one);
                denominator_exponent = leading_one-149;
            }else{
                normalized_significand[23] = 1;
                normalized_significand.range(22, 0) = fraction;
                denominator_exponent = exponent.to_uint()-127;
            }

            state.divisor = normalized_significand;

            if(normalized_significand == ((ap_uint<24>)1 << 23)){
                // 输入有效数恰好为1.0。直接保存精确的1.000...商，
                // 但仍经过固定数量的ITER周期以保持统一latency。
                state.exact_power_of_two = true;
                state.quotient = (ap_uint<26>)1 << 25;
                state.remainder = 0;
                state.result_exponent = -denominator_exponent;
            }else{
                /**
                 * denominator = M*2^e，M位于(1,2)。
                 * reciprocal = (2/M)*2^(-e-1)。
                 *
                 * 因此尾数恢复除法计算2/M：分子2.0对应第24位为1，
                 * 除数M对应24位1.fraction。
                 */
                state.remainder = (ap_uint<25>)1 << 24;
                state.quotient = 0;
                state.result_exponent = -denominator_exponent-1;
            }
        }

        /**
         * @brief 把26位商和剩余余数按RNE规则打包成IEEE-754 FP32
         */
        acc_t normalize_and_round(
            const ReciprocalDividerState& state
        ){
            if(state.special){
                return acc_float_from_bits(state.special_result_bits);
            }

            int result_exponent = state.result_exponent.to_int();
            const ap_uint<26> quotient = state.quotient;
            const bool remainder_sticky = state.remainder != 0;
            ap_uint<32> result_bits = 0;
            result_bits[31] = state.result_sign;

            if(result_exponent > 127){
                // 上溢为带正确符号的Infinity。
                result_bits.range(30, 23) = (ap_uint<8>)0xff;
                return acc_float_from_bits(result_bits);
            }

            if(result_exponent >= -126){
                // 规格化数：保留隐藏位和23位fraction。
                const ap_uint<24> retained = quotient.range(25, 2);
                const bool guard = quotient[1];
                const bool round = quotient[0];
                const bool increment =
                    guard && (round || remainder_sticky || retained[0]);

                ap_uint<25> rounded = retained;
                if(increment){
                    rounded += 1;
                }

                // 1.111...向上舍入可能变成10.000...，需要右移并加指数。
                if(rounded[24]){
                    rounded >>= 1;
                    ++result_exponent;
                    if(result_exponent > 127){
                        result_bits.range(30, 23) = (ap_uint<8>)0xff;
                        return acc_float_from_bits(result_bits);
                    }
                }

                result_bits.range(30, 23) =
                    (ap_uint<8>)(result_exponent+127);
                result_bits.range(22, 0) = rounded.range(22, 0);
                return acc_float_from_bits(result_bits);
            }

            /**
             * 次正规结果没有隐藏位。quotient的二进制小数点位于bit25
             * 之后；为了转换成2^-149为单位的fraction，需要继续右移：
             *
             *     subnormal_shift = -result_exponent-124
             */
            const int subnormal_shift = -result_exponent-124;
            ap_uint<24> truncated = 0;
            bool guard = false;
            bool sticky = remainder_sticky;

            if(subnormal_shift>0 &&
                    subnormal_shift<=reciprocalQuotientBits){
                truncated = (ap_uint<24>)(quotient >> subnormal_shift);

                // 使用静态bit索引生成guard/sticky，避免变量bit-select影响
                // QoR；subnormal_shift的显式下界也排除了bit -1。
                for(int bit=0; bit<reciprocalQuotientBits; ++bit){
                    #pragma HLS UNROLL
                    if(bit==subnormal_shift-1){
                        guard = quotient[bit];
                    }else if(bit<subnormal_shift-1 && quotient[bit]){
                        sticky = true;
                    }
                }
            }

            const bool increment = guard && (sticky || truncated[0]);
            ap_uint<25> rounded_subnormal = truncated;
            if(increment){
                rounded_subnormal += 1;
            }

            if(rounded_subnormal[23]){
                // 最大次正规数向上舍入后成为最小规格化数。
                result_bits.range(30, 23) = (ap_uint<8>)1;
                result_bits.range(22, 0) = 0;
            }else{
                result_bits.range(30, 23) = 0;
                result_bits.range(22, 0) =
                    rounded_subnormal.range(22, 0);
            }
            return acc_float_from_bits(result_bits);
        }

        /**
         * @brief 恢复除法器的一个逻辑时钟步骤
         *
         * 状态由外层AccumulatorState保存，因此这里不能再使用函数内static。
         * 每列分别调用本函数，综合展开后对应相互独立的除法状态。
         */
        ReciprocalTickOutput divider_tick(
            const ReciprocalDividerState& current,
            ReciprocalDividerState& next,
            const bool start,
            const acc_t denominator
        ){
            ReciprocalTickOutput output{};
            next = current;

            switch(current.phase){
            case ReciprocalPhase::IDLE:
                if(start){
                    begin_reciprocal(next, denominator);
                }
                break;

            case ReciprocalPhase::ITER:{
                ap_uint<25> remainder = current.remainder;
                ap_uint<26> quotient = current.quotient;

                if(!current.special && !current.exact_power_of_two){
                    // 一拍内组合展开两次，产生两个商位。
                    for(int bit=0; bit<reciprocalBitsPerCycle; ++bit){
                        #pragma HLS UNROLL
                        div_step(
                            remainder,
                            quotient,
                            current.divisor
                        );
                    }
                }

                next.remainder = remainder;
                next.quotient = quotient;

                if(current.iter_count ==
                        (reciprocal_iter_count_t)(reciprocalIterationCycles-1)){
                    next.phase = ReciprocalPhase::DONE;
                }else{
                    next.iter_count = current.iter_count+1;
                }
                break;
            }

            case ReciprocalPhase::DONE:
                output.valid = true;
                output.value = normalize_and_round(current);
                next.result = output.value;
                next.phase = ReciprocalPhase::IDLE;
                break;
            }

            return output;
        }

        /**
         * @brief 推进一列Accumulator状态并产生该列SRAM写回数据
         *
         * lane是展开循环传入的编译期常量。FUNCTION_INSTANTIATE让
         * Vitis HLS为每个lane生成不同的RTL实现；INLINE off继续保留
         * 每列独立层次，避免PWL、FP32乘加和恢复除法器跨列复用。
         */
        void accumulator_lane_step(
            const int lane,
            const acc_t current_scale,
            const ReciprocalDividerState& current_reciprocal,
            acc_t& next_scale,
            ReciprocalDividerState& next_reciprocal,
            const bool valid,
            const AccumulatorCmd cmd,
            const acc_t sa_in,
            const acc_t sram_in,
            acc_t& sram_out
        ){
            #pragma HLS INLINE off
            #pragma HLS FUNCTION_INSTANTIATE variable=lane

            // 每个lane只读本列current，并只写本列next。
            next_scale = current_scale;
            next_reciprocal = current_reciprocal;

            const bool exp_s1 = cmd == AccumulatorCmd::EXP_S1;
            const bool exp_s2 = cmd == AccumulatorCmd::EXP_S2;
            const bool acc_sa = cmd == AccumulatorCmd::ACC_SA;
            const bool set = cmd == AccumulatorCmd::SET_SCALE;
            const bool reciprocal_cmd = cmd == AccumulatorCmd::RECIPROCAL;

            const acc_t in_a = exp_s1 ? sa_in : current_scale;
            const acc_t in_b = exp_s1 ? attentionScale() : sram_in;
            const acc_t in_c = acc_sa ? sa_in : accZero();

            acc_t unit_output = exp_s2 ? accExp2PWL(in_a)
                                    : accUnit(in_a, in_b, in_c);

            const ReciprocalTickOutput reciprocal_output = divider_tick(
                current_reciprocal,
                next_reciprocal,
                valid && reciprocal_cmd,
                current_scale
            );

            if(reciprocal_output.valid){
                unit_output = reciprocal_output.value;
            }

            sram_out = unit_output;

            // reciprocal结果完成的优先级最高，而且不依赖请求valid持续置位。
            if(reciprocal_output.valid){
                next_scale = unit_output;
            }else if(valid){
                if(exp_s1 || exp_s2){
                    next_scale = unit_output;
                }else if(set){
                    next_scale = sram_in;
                }
            }
        }

    }  // namespace

    void reset_accumulator_state(AccumulatorState& state){
        #pragma HLS INLINE

        #pragma HLS ARRAY_PARTITION variable=state.scale \
            type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=state.reciprocal \
            type=complete dim=1
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            state.scale[col] = accZero();
            state.reciprocal[col] = ReciprocalDividerState{};
        }
        return;
    }

    void accumulator_step(const AccumulatorState& current,
                        AccumulatorState& next, AccumulatorIO& io){
        
        #pragma HLS INLINE off
        #pragma HLS LATENCY max=18

        #pragma HLS ARRAY_PARTITION variable=current.scale \
            type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=current.reciprocal \
            type=complete dim=1

        #pragma HLS ARRAY_PARTITION variable=next.scale \
            type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=next.reciprocal \
            type=complete dim=1

        #pragma HLS ARRAY_PARTITION variable=io.sa_in \
            type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=io.sram_in \
            type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=io.sram_out \
            type=complete dim=1

        const bool valid = io.ctrl_in.valid;
        const AccumulatorCmd cmd = io.ctrl_in.bits.cmd;

        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            accumulator_lane_step(
                col,
                current.scale[col], current.reciprocal[col],
                next.scale[col], next.reciprocal[col], valid, cmd,
                io.sa_in[col], io.sram_in[col], io.sram_out[col]
            );
        }
    }

}  // namespace fsa
