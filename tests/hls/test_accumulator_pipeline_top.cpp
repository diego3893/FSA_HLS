/**
 * @file test_accumulator_pipeline_top.cpp
 * @brief 新流水Accumulator批处理HLS顶层的C/RTL共同testbench
 */

#include <cmath>
#include <cstddef>
#include <iostream>

#include "fsa/hls/accumulator_pipeline_top.hpp"

namespace{

bool almostEqual(
    const fsa::acc_t actual,
    const fsa::acc_t expected,
    const fsa::acc_t tolerance = (fsa::acc_t)1.0e-5F
){
    return std::fabs(actual-expected) <=
        tolerance*((fsa::acc_t)1.0F+std::fabs(expected));
}

}  // namespace

int main(){
    fsa::AccumulatorToken
        input[fsa::accumulatorPipelineBatchCycles]{};
    fsa::AccumulatorPipelineOutput
        output[fsa::accumulatorPipelineBatchCycles]{};

    input[0].valid = true;
    input[0].cmd = fsa::AccumulatorCmd::SET_SCALE;
    input[0].sram_in = {{1.0F, 2.0F, 3.0F, 4.0F}};

    constexpr int first_acc_cycle = 1;
    constexpr int token_count = 64;
    for(int index=0; index<token_count; ++index){
        const int cycle = first_acc_cycle+index;
        input[cycle].valid = true;
        input[cycle].cmd = fsa::AccumulatorCmd::ACC_SA;
        input[cycle].write_enable = true;
        input[cycle].write_addr = (fsa::sram_address_t)(index&31);
        input[cycle].tag = (ap_uint<8>)index;
        for(int col=0; col<fsa::SA_COLS; ++col){
            input[cycle].sram_in[(std::size_t)col] =
                (fsa::acc_t)(index+1);
            input[cycle].sa_in[(std::size_t)col] =
                (fsa::acc_t)(col-index);
        }
    }

    // 在fast流水排空后实际执行一次四列EXP_S2，并用后续ACC读回scale。
    constexpr int slow_set_cycle = 74;
    constexpr int exp2_cycle = slow_set_cycle+1;
    constexpr int slow_done_cycle = exp2_cycle+
        fsa::accumulatorExp2Latency;
    constexpr int post_exp2_acc_cycle = slow_done_cycle+1;
    constexpr int post_exp2_result_cycle = post_exp2_acc_cycle+
        fsa::accumulatorFastLatency;

    input[slow_set_cycle].valid = true;
    input[slow_set_cycle].cmd = fsa::AccumulatorCmd::SET_SCALE;
    input[slow_set_cycle].sram_in = {{0.0F, 1.0F, -1.0F, 2.0F}};

    input[exp2_cycle].valid = true;
    input[exp2_cycle].cmd = fsa::AccumulatorCmd::EXP_S2;

    input[post_exp2_acc_cycle].valid = true;
    input[post_exp2_acc_cycle].cmd = fsa::AccumulatorCmd::ACC;
    input[post_exp2_acc_cycle].sram_in = {{1.0F, 1.0F, 1.0F, 1.0F}};

    accumulator_pipeline_batch_top(true, input, output);

    int failures = 0;
    if(!output[0].input_ready){
        std::cerr << "[FAIL] SET_SCALE was not accepted" << std::endl;
        ++failures;
    }

    for(int index=0; index<token_count; ++index){
        const int cycle = first_acc_cycle+index;
        if(!output[cycle].input_ready || output[cycle].scale_busy ||
                output[cycle].slow_done){
            std::cerr << "[FAIL] fast input was not accepted at cycle="
                      << cycle << std::endl;
            ++failures;
        }
    }

    if(!output[slow_set_cycle].input_ready ||
            !output[exp2_cycle].input_ready ||
            !output[exp2_cycle].scale_busy){
        std::cerr << "[FAIL] EXP_S2 was not accepted" << std::endl;
        ++failures;
    }

    for(int cycle=exp2_cycle+1; cycle<slow_done_cycle; ++cycle){
        if(output[cycle].input_ready || !output[cycle].scale_busy ||
                output[cycle].slow_done){
            std::cerr << "[FAIL] EXP_S2 busy window cycle="
                      << cycle << std::endl;
            ++failures;
        }
    }

    if(output[slow_done_cycle].input_ready ||
            output[slow_done_cycle].scale_busy ||
            !output[slow_done_cycle].slow_done ||
            !output[post_exp2_acc_cycle].input_ready){
        std::cerr << "[FAIL] EXP_S2 completion handshake" << std::endl;
        ++failures;
    }

    const fsa::AccVector exp2_expected = {{1.0F, 2.0F, 0.5F, 4.0F}};
    if(!output[post_exp2_result_cycle].result.valid){
        std::cerr << "[FAIL] post-EXP_S2 result missing" << std::endl;
        ++failures;
    }else{
        for(int col=0; col<fsa::SA_COLS; ++col){
            if(!almostEqual(
                    output[post_exp2_result_cycle]
                        .result.data[(std::size_t)col],
                    exp2_expected[(std::size_t)col],
                    (fsa::acc_t)1.0e-3F)){
                std::cerr << "[FAIL] EXP_S2 data col=" << col << std::endl;
                ++failures;
            }
        }
    }

    for(int index=0; index<token_count; ++index){
        const int cycle = first_acc_cycle+index+
            fsa::accumulatorFastLatency;
        if(!output[cycle].result.valid ||
                !output[cycle].result.write_enable ||
                output[cycle].result.tag != (ap_uint<8>)index ||
                output[cycle].result.write_addr !=
                    (fsa::sram_address_t)(index&31)){
            std::cerr << "[FAIL] metadata index=" << index << std::endl;
            ++failures;
            continue;
        }

        for(int col=0; col<fsa::SA_COLS; ++col){
            const fsa::acc_t scale = (fsa::acc_t)(col+1);
            const fsa::acc_t expected = scale*(fsa::acc_t)(index+1)
                +(fsa::acc_t)(col-index);
            if(!almostEqual(
                    output[cycle].result.data[(std::size_t)col], expected)){
                std::cerr << "[FAIL] data index=" << index
                          << ", col=" << col << std::endl;
                ++failures;
            }
        }
    }

    if(failures!=0){
        return 1;
    }

    std::cout << "[PASS] test_accumulator_pipeline_top: 64 contiguous "
                 "ACC_SA tokens plus four-lane EXP_S2"
              << std::endl;
    return 0;
}
