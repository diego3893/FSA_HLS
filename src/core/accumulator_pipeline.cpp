#include "fsa/accumulator_pipeline.hpp"

#include "fsa/arithmetic.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace{

    struct ReciprocalTickOutput{
        bool valid = false;
        acc_t value = 0.0F;
    };

    acc_t accFloatFromBits(const ap_uint<32> bits){
        const fp_struct<acc_t> view(bits);
        return view.to_ieee();
    }

    void divStep(
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
     * @brief 初始化与旧accumulator_step相同的FP32恢复除法状态
     *
     * 本实现留在新文件中，避免改动旧Accumulator及FSA_core链接关系。
     */
    void beginReciprocal(
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

        if(exponent == (ap_uint<8>)0xff){
            state.special = true;
            if(fraction != 0){
                state.special_result_bits = (ap_uint<32>)0x7fc00000;
            }else{
                state.special_result_bits = ((ap_uint<32>)sign) << 31;
            }
            return;
        }

        if(exponent == 0 && fraction == 0){
            state.special = true;
            state.special_result_bits =
                (((ap_uint<32>)sign) << 31) | (ap_uint<32>)0x7f800000;
            return;
        }

        ap_uint<24> normalized_significand = 0;
        int denominator_exponent = 0;

        if(exponent == 0){
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
            state.exact_power_of_two = true;
            state.quotient = (ap_uint<26>)1 << 25;
            state.remainder = 0;
            state.result_exponent = -denominator_exponent;
        }else{
            state.remainder = (ap_uint<25>)1 << 24;
            state.quotient = 0;
            state.result_exponent = -denominator_exponent-1;
        }
    }

    acc_t normalizeAndRound(const ReciprocalDividerState& state){
        if(state.special){
            return accFloatFromBits(state.special_result_bits);
        }

        int result_exponent = state.result_exponent.to_int();
        const ap_uint<26> quotient = state.quotient;
        const bool remainder_sticky = state.remainder != 0;
        ap_uint<32> result_bits = 0;
        result_bits[31] = state.result_sign;

        if(result_exponent > 127){
            result_bits.range(30, 23) = (ap_uint<8>)0xff;
            return accFloatFromBits(result_bits);
        }

        if(result_exponent >= -126){
            const ap_uint<24> retained = quotient.range(25, 2);
            const bool guard = quotient[1];
            const bool round = quotient[0];
            const bool increment =
                guard && (round || remainder_sticky || retained[0]);

            ap_uint<25> rounded = retained;
            if(increment){
                rounded += 1;
            }

            if(rounded[24]){
                rounded >>= 1;
                ++result_exponent;
                if(result_exponent > 127){
                    result_bits.range(30, 23) = (ap_uint<8>)0xff;
                    return accFloatFromBits(result_bits);
                }
            }

            result_bits.range(30, 23) =
                (ap_uint<8>)(result_exponent+127);
            result_bits.range(22, 0) = rounded.range(22, 0);
            return accFloatFromBits(result_bits);
        }

        const int subnormal_shift = -result_exponent-124;
        ap_uint<24> truncated = 0;
        bool guard = false;
        bool sticky = remainder_sticky;

        if(subnormal_shift <= reciprocalQuotientBits){
            truncated = (ap_uint<24>)(quotient >> subnormal_shift);
            guard = quotient[subnormal_shift-1];
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
            result_bits.range(30, 23) = (ap_uint<8>)1;
            result_bits.range(22, 0) = 0;
        }else{
            result_bits.range(30, 23) = 0;
            result_bits.range(22, 0) = rounded_subnormal.range(22, 0);
        }
        return accFloatFromBits(result_bits);
    }

    ReciprocalTickOutput reciprocalTick(
        const ReciprocalDividerState& current,
        ReciprocalDividerState& next,
        const bool start,
        const acc_t denominator
    ){
        #pragma HLS INLINE

        ReciprocalTickOutput output{};
        next = current;

        switch(current.phase){
        case ReciprocalPhase::IDLE:
            if(start){
                beginReciprocal(next, denominator);
            }
            break;

        case ReciprocalPhase::ITER:{
            ap_uint<25> remainder = current.remainder;
            ap_uint<26> quotient = current.quotient;

            if(!current.special && !current.exact_power_of_two){
                for(int bit=0; bit<reciprocalBitsPerCycle; ++bit){
                    #pragma HLS UNROLL
                    divStep(remainder, quotient, current.divisor);
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
            output.value = normalizeAndRound(current);
            next.result = output.value;
            next.phase = ReciprocalPhase::IDLE;
            break;
        }

        return output;
    }

    void fastLane(
        const int lane,
        const AccumulatorCmd cmd,
        const acc_t scale,
        const acc_t sa_in,
        const acc_t sram_in,
        acc_t& result
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS FUNCTION_INSTANTIATE variable=lane
        (void)lane;

        const bool exp_s1 = cmd == AccumulatorCmd::EXP_S1;
        const bool acc_sa = cmd == AccumulatorCmd::ACC_SA;
        const acc_t in_a = exp_s1 ? sa_in : scale;
        const acc_t in_b = exp_s1 ? attentionScale() : sram_in;
        const acc_t in_c = acc_sa ? sa_in : accZero();
        result = accUnit(in_a, in_b, in_c);
    }

    void exp2Lane(
        const int lane,
        const acc_t input,
        acc_t& result
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS FUNCTION_INSTANTIATE variable=lane
        (void)lane;

        result = accExp2PWL(input);
    }

    bool isFastFmaCommand(const AccumulatorCmd cmd){
        #pragma HLS INLINE
        return cmd == AccumulatorCmd::ACC ||
               cmd == AccumulatorCmd::ACC_SA ||
               cmd == AccumulatorCmd::EXP_S1;
    }

}  // namespace

void reset_accumulator_pipeline_state(AccumulatorPipelineState& state){
    #pragma HLS INLINE
    state = AccumulatorPipelineState{};
}

void accumulator_pipeline_tick(
    const AccumulatorPipelineState& current,
    AccumulatorPipelineState& next,
    const AccumulatorToken& input,
    AccumulatorPipelineOutput& output
){
    #pragma HLS INLINE

    static_assert(
        SA_COLS == 4,
        "当前流水Accumulator硬件层次显式实现四个独立lane"
    );

    #pragma HLS ARRAY_PARTITION variable=current.scale type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.fast_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.exp2_result type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.reciprocal type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.scale type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.fast_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.exp2_result type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.reciprocal type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=input.sa_in type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=input.sram_in type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=output.result.data type=complete dim=1

    next = current;
    output = AccumulatorPipelineOutput{};

    // 快速结果和全部RMW元数据一起移位并从最后一级输出。
    output.result = current.fast_pipe[accumulatorFastLatency-1].result;
    for(int stage=accumulatorFastLatency-1; stage>0; --stage){
        #pragma HLS UNROLL
        next.fast_pipe[stage] = current.fast_pipe[stage-1];
    }
    next.fast_pipe[0] = AccumulatorFastStage{};

    if(current.fast_pipe[accumulatorFastLatency-1].scale_update){
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            next.scale[col] =
                current.fast_pipe[accumulatorFastLatency-1]
                    .result.data[(std::size_t)col];
        }
        next.scale_update_pending = false;
    }

    // 慢路径每tick后台推进；运行期间对所有依赖scale的命令背压。
    if(current.scale_busy){
        if(current.slow_operation == AccumulatorSlowOperation::EXP_S2){
            if(current.exp2_countdown <= 1){
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    next.scale[col] = current.exp2_result[col];
                }
                next.scale_busy = false;
                next.slow_operation = AccumulatorSlowOperation::NONE;
                next.exp2_countdown = 0;
                output.slow_done = true;
            }else{
                next.exp2_countdown = current.exp2_countdown-1;
            }
        }else if(current.slow_operation ==
                    AccumulatorSlowOperation::RECIPROCAL){
            ReciprocalTickOutput reciprocal_output[SA_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=reciprocal_output type=complete dim=1

            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                reciprocal_output[col] = reciprocalTick(
                    current.reciprocal[col],
                    next.reciprocal[col],
                    false,
                    current.scale[col]
                );
            }

            bool all_done = true;
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                all_done = all_done && reciprocal_output[col].valid;
            }
            if(all_done){
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    next.scale[col] = reciprocal_output[col].value;
                }
                next.scale_busy = false;
                next.slow_operation = AccumulatorSlowOperation::NONE;
                output.slow_done = true;
            }
        }
    }

    // ready只依赖本tick开始时的已提交状态，完成tick后保守地空一拍。
    output.input_ready =
        !current.scale_update_pending && !current.scale_busy;

    const bool accepted = input.valid && output.input_ready;
    if(accepted){
        if(isFastFmaCommand(input.cmd)){
            AccumulatorFastStage stage{};
            stage.result.valid = true;
            stage.result.write_addr = input.write_addr;
            stage.result.write_enable = input.write_enable;
            stage.result.tag = input.tag;
            stage.scale_update = input.cmd == AccumulatorCmd::EXP_S1;

            fastLane(
                0, input.cmd, current.scale[0],
                input.sa_in[0], input.sram_in[0], stage.result.data[0]
            );
            fastLane(
                1, input.cmd, current.scale[1],
                input.sa_in[1], input.sram_in[1], stage.result.data[1]
            );
            fastLane(
                2, input.cmd, current.scale[2],
                input.sa_in[2], input.sram_in[2], stage.result.data[2]
            );
            fastLane(
                3, input.cmd, current.scale[3],
                input.sa_in[3], input.sram_in[3], stage.result.data[3]
            );
            next.fast_pipe[0] = stage;

            if(stage.scale_update){
                next.scale_update_pending = true;
            }
        }else if(input.cmd == AccumulatorCmd::SET_SCALE){
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                next.scale[col] = input.sram_in[(std::size_t)col];
            }
        }else if(input.cmd == AccumulatorCmd::EXP_S2){
            exp2Lane(0, current.scale[0], next.exp2_result[0]);
            exp2Lane(1, current.scale[1], next.exp2_result[1]);
            exp2Lane(2, current.scale[2], next.exp2_result[2]);
            exp2Lane(3, current.scale[3], next.exp2_result[3]);
            next.exp2_countdown = accumulatorExp2Latency;
            next.slow_operation = AccumulatorSlowOperation::EXP_S2;
            next.scale_busy = true;
        }else if(input.cmd == AccumulatorCmd::RECIPROCAL){
            for(int col=0; col<SA_COLS; ++col){
                #pragma HLS UNROLL
                reciprocalTick(
                    current.reciprocal[col],
                    next.reciprocal[col],
                    true,
                    current.scale[col]
                );
            }
            next.slow_operation = AccumulatorSlowOperation::RECIPROCAL;
            next.scale_busy = true;
        }
    }

    output.scale_busy = next.scale_busy;
}

}  // namespace fsa
