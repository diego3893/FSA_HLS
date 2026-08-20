/**
 * @file test_accumulator_pipeline_top.cpp
 * @brief 新流水Accumulator批处理HLS顶层的C/RTL共同testbench
 */

#include <cmath>
#include <cstddef>
#include <iostream>

#include "fsa/hls/accumulator_pipeline_top.hpp"

namespace{

bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual-expected) <=
        (fsa::acc_t)1.0e-5F*((fsa::acc_t)1.0F+std::fabs(expected));
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

    accumulator_pipeline_batch_top(true, input, output);

    int failures = 0;
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
                 "ACC_SA tokens with aligned result/address/tag"
              << std::endl;
    return 0;
}
