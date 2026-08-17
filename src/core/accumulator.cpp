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

            if(subnormal_shift <= reciprocalQuotientBits){
                truncated = (ap_uint<24>)(quotient >> subnormal_shift);
                guard = quotient[subnormal_shift-1];

                // guard以下的所有商位与未除尽余数共同形成sticky。
                for(int bit=0; bit<reciprocalQuotientBits; ++bit){
                    #pragma HLS UNROLL
                    if(bit < subnormal_shift-1 && quotient[bit]){
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
         * 四列分别调用本函数，综合展开后对应四套相互独立的除法状态。
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

    }  // namespace

    void reset_accumulator_state(AccumulatorState& state){
        for(int col=0; col<SA_COLS; ++col){
            // MOD: 四列复位寄存器同拍清零，避免生成四拍复位循环。
            #pragma HLS UNROLL
            state.scale[col] = accZero();
            state.reciprocal[col] = ReciprocalDividerState{};
        }
        return;
    }

    void accumulator_step(const AccumulatorState& current,
                        AccumulatorState& next, AccumulatorIO& io){
        // MOD: 内联普通命令路径，使列UNROLL与顶层调度作用于同一层级。
        #pragma HLS INLINE

        next = current;

        // MOD: 无写回时保持确定值；下游只在sram_write_valid时写SRAM。
        io.command_ready = true;
        io.sram_write_valid = false;
        io.reciprocal_busy = false;
        io.reciprocal_result_valid = false;

        bool current_reciprocal_busy = false;
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            io.sram_out[(std::size_t)col] = current.scale[col];
            current_reciprocal_busy = current_reciprocal_busy
                || current.reciprocal[col].phase!=ReciprocalPhase::IDLE;
        }

        const bool valid = io.ctrl_in.valid;
        const AccumulatorCmd cmd = io.ctrl_in.bits.cmd;
        const bool reciprocal_cmd = cmd == AccumulatorCmd::RECIPROCAL;

        /**
         * MOD: reciprocal运行期间拒绝所有新命令，四列FSM仍每拍自动推进。
         * command_ready描述本拍输入是否会被接受；reciprocal_busy描述提交
         * next状态后是否仍有在途请求。
         */
        io.command_ready = !current_reciprocal_busy;
        const bool start_reciprocal =
            valid && reciprocal_cmd && io.command_ready;

        if(current_reciprocal_busy || start_reciprocal){
            bool all_results_valid = true;
            bool any_next_busy = false;

            for(int col=0; col<SA_COLS; ++col){
                // MOD: complete UNROLL生成四套独立恢复除法数据通路。
                #pragma HLS UNROLL
                const ReciprocalTickOutput reciprocal_output = divider_tick(
                    current.reciprocal[col],
                    next.reciprocal[col],
                    start_reciprocal,
                    current.scale[col]
                );

                all_results_valid = all_results_valid
                    && reciprocal_output.valid;

                if(reciprocal_output.valid){
                    io.sram_out[(std::size_t)col] = reciprocal_output.value;
                    next.scale[col] = reciprocal_output.value;
                }

                any_next_busy = any_next_busy
                    || next.reciprocal[col].phase!=ReciprocalPhase::IDLE;
            }

            io.reciprocal_result_valid = all_results_valid;
            // reciprocal只更新内部scale，不直接触发Accumulator SRAM写回。
            io.sram_write_valid = false;
            io.reciprocal_busy = any_next_busy;
            return;
        }

        if(!valid){
            return;
        }

        /**
         * MOD: 主四列计算循环完全展开。这里使用按命令分支，而不是先无条件
         * 计算所有候选值，可避免invalid/reciprocal拍无意义地启动浮点单元。
         */
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            const std::size_t index = (std::size_t)col;

            switch(cmd){
            case AccumulatorCmd::EXP_S1:
                io.sram_out[index] = accUnit(
                    io.sa_in[index],
                    attentionScale(),
                    accZero()
                );
                next.scale[col] = io.sram_out[index];
                break;

            case AccumulatorCmd::EXP_S2:
                io.sram_out[index] = accExp2PWL(current.scale[col]);
                next.scale[col] = io.sram_out[index];
                break;

            case AccumulatorCmd::ACC_SA:
                io.sram_out[index] = accUnit(
                    current.scale[col],
                    io.sram_in[index],
                    io.sa_in[index]
                );
                break;

            case AccumulatorCmd::ACC:
                io.sram_out[index] = accUnit(
                    current.scale[col],
                    io.sram_in[index],
                    accZero()
                );
                break;

            case AccumulatorCmd::SET_SCALE:
                next.scale[col] = io.sram_in[index];
                break;

            case AccumulatorCmd::RECIPROCAL:
                // 已在上面的start_reciprocal分支处理。
                break;

            default:
                // MOD: 非法3位命令编码不更新任何状态，也不产生写回。
                break;
            }
        }

        // MOD: 只有ACC/ACC_SA对应Accumulator SRAM读改写。
        io.sram_write_valid = cmd==AccumulatorCmd::ACC
            || cmd==AccumulatorCmd::ACC_SA;
    }

    void accumulator_reciprocal_transaction(
        acc_t scale[SA_COLS],
        AccVector& result
    ){
        /**
         * MOD: 保持函数边界，生成可在综合报告中单独核对的固定延迟模块。
         * LATENCY是约束而非证明；构建后必须确认实际Latency恰好为15。
         */
        #pragma HLS INLINE off
        #pragma HLS LATENCY min=15 max=15

        ReciprocalDividerState lane_state[SA_COLS];
        #pragma HLS ARRAY_PARTITION variable=lane_state \
            type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=result \
            type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=scale \
            type=complete dim=1

        /**
         * 15次循环迭代分别对应：
         *   step 0      ：IDLE接收请求；
         *   step 1..13  ：13次ITER，每拍产生两个商位；
         *   step 14     ：DONE规格化、RNE舍入并输出。
         */
        for(int step=0; step<reciprocalLatency; ++step){
            #pragma HLS PIPELINE II=1

            for(int col=0; col<SA_COLS; ++col){
                // MOD: 四列同时推进，生成四套独立reciprocal数据通路。
                #pragma HLS UNROLL
                if(step==0){
                    begin_reciprocal(lane_state[col], scale[col]);
                }else if(step<=reciprocalIterationCycles){
                    ap_uint<25> remainder = lane_state[col].remainder;
                    ap_uint<26> quotient = lane_state[col].quotient;

                    if(!lane_state[col].special
                            && !lane_state[col].exact_power_of_two){
                        for(int bit=0; bit<reciprocalBitsPerCycle; ++bit){
                            #pragma HLS UNROLL
                            div_step(
                                remainder,
                                quotient,
                                lane_state[col].divisor
                            );
                        }
                    }

                    lane_state[col].remainder = remainder;
                    lane_state[col].quotient = quotient;
                }else{
                    const acc_t reciprocal_value =
                        normalize_and_round(lane_state[col]);
                    result[(std::size_t)col] = reciprocal_value;
                    scale[col] = reciprocal_value;
                }
            }
        }
    }

}  // namespace fsa
