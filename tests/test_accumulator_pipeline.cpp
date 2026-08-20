/**
 * @file test_accumulator_pipeline.cpp
 * @brief 新Accumulator token流水的纯C++时序、hazard和数值测试
 */

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

#include "fsa/accumulator_pipeline.hpp"
#include "fsa/arithmetic.hpp"

namespace{

bool fpEqual(
    const fsa::acc_t actual,
    const fsa::acc_t expected,
    const fsa::acc_t tolerance = (fsa::acc_t)1.0e-5F
){
    if(std::isnan(expected)){
        return std::isnan(actual);
    }
    if(std::isinf(expected)){
        return std::isinf(actual) &&
            std::signbit(actual) == std::signbit(expected);
    }
    if(expected == (fsa::acc_t)0.0F){
        return actual == (fsa::acc_t)0.0F &&
            std::signbit(actual) == std::signbit(expected);
    }
    return std::fabs(actual-expected) <=
        tolerance*((fsa::acc_t)1.0F+std::fabs(expected));
}

void expectVector(
    const fsa::AccVector& actual,
    const fsa::AccVector& expected,
    const fsa::acc_t tolerance = (fsa::acc_t)1.0e-5F
){
    for(int col=0; col<fsa::SA_COLS; ++col){
        assert(fpEqual(
            actual[(std::size_t)col],
            expected[(std::size_t)col],
            tolerance
        ));
    }
}

fsa::AccumulatorPipelineOutput tick(
    fsa::AccumulatorPipelineState& state,
    const fsa::AccumulatorToken& input = fsa::AccumulatorToken{}
){
    fsa::AccumulatorPipelineState next{};
    fsa::AccumulatorPipelineOutput output{};
    fsa::accumulator_pipeline_tick(state, next, input, output);
    state = next;
    return output;
}

fsa::AccumulatorToken command(const fsa::AccumulatorCmd cmd){
    fsa::AccumulatorToken token{};
    token.valid = true;
    token.cmd = cmd;
    return token;
}

void setScale(
    fsa::AccumulatorPipelineState& state,
    const fsa::AccVector& scale
){
    fsa::AccumulatorToken token = command(fsa::AccumulatorCmd::SET_SCALE);
    token.sram_in = scale;
    const fsa::AccumulatorPipelineOutput output = tick(state, token);
    assert(output.input_ready);
}

fsa::AccumulatorPipelineOutput waitForResult(
    fsa::AccumulatorPipelineState& state
){
    for(int cycle=0; cycle<=fsa::accumulatorFastLatency; ++cycle){
        const fsa::AccumulatorPipelineOutput output = tick(state);
        if(output.result.valid){
            return output;
        }
    }
    assert(false && "fast result did not arrive");
    return fsa::AccumulatorPipelineOutput{};
}

void waitUntilReady(fsa::AccumulatorPipelineState& state){
    for(int cycle=0; cycle<128; ++cycle){
        const fsa::AccumulatorPipelineOutput output = tick(state);
        if(output.input_ready){
            return;
        }
    }
    assert(false && "pipeline did not become ready");
}

}  // namespace

int main(){
    static_assert(fsa::SA_COLS == 4, "测试向量按四列编写");

    fsa::AccumulatorPipelineState state{};
    fsa::reset_accumulator_pipeline_state(state);

    for(int col=0; col<fsa::SA_COLS; ++col){
        assert(state.scale[col] == (fsa::acc_t)0.0F);
    }
    assert(tick(state).input_ready);

    const fsa::AccVector scale = {{
        (fsa::acc_t)1.0F,
        (fsa::acc_t)2.0F,
        (fsa::acc_t)3.0F,
        (fsa::acc_t)4.0F
    }};
    setScale(state, scale);

    // 连续64个ACC无bubble：每tick都接受，结果、地址和tag保持顺序。
    for(int index=0; index<64+fsa::accumulatorFastLatency; ++index){
        fsa::AccumulatorToken token{};
        if(index<64){
            token = command(fsa::AccumulatorCmd::ACC);
            token.write_enable = true;
            token.write_addr = (fsa::sram_address_t)(index&31);
            token.tag = (ap_uint<8>)index;
            for(int col=0; col<fsa::SA_COLS; ++col){
                token.sram_in[(std::size_t)col] =
                    (fsa::acc_t)(index+col+1);
            }
        }

        const fsa::AccumulatorPipelineOutput output = tick(state, token);
        if(index<64){
            assert(output.input_ready);
        }

        if(index>=fsa::accumulatorFastLatency){
            const int expected_index = index-fsa::accumulatorFastLatency;
            if(expected_index<64){
                assert(output.result.valid);
                assert(output.result.write_enable);
                assert(output.result.write_addr ==
                    (fsa::sram_address_t)(expected_index&31));
                assert(output.result.tag == (ap_uint<8>)expected_index);
                for(int col=0; col<fsa::SA_COLS; ++col){
                    const fsa::acc_t expected = scale[(std::size_t)col]
                        *(fsa::acc_t)(expected_index+col+1);
                    assert(fpEqual(
                        output.result.data[(std::size_t)col], expected
                    ));
                }
            }
        }
    }

    // 带bubble的ACC_SA仍保持token元数据和数值对齐。
    for(int cycle=0; cycle<24+fsa::accumulatorFastLatency; ++cycle){
        fsa::AccumulatorToken token{};
        if(cycle<24 && cycle%3!=1){
            token = command(fsa::AccumulatorCmd::ACC_SA);
            token.tag = (ap_uint<8>)cycle;
            token.write_addr = (fsa::sram_address_t)(cycle&31);
            token.write_enable = true;
            for(int col=0; col<fsa::SA_COLS; ++col){
                token.sram_in[(std::size_t)col] = (fsa::acc_t)(col+1);
                token.sa_in[(std::size_t)col] = (fsa::acc_t)(cycle-col);
            }
        }

        const fsa::AccumulatorPipelineOutput output = tick(state, token);
        if(cycle>=fsa::accumulatorFastLatency){
            const int source_cycle = cycle-fsa::accumulatorFastLatency;
            const bool expected_valid =
                source_cycle<24 && source_cycle%3!=1;
            assert(output.result.valid == expected_valid);
            if(expected_valid){
                assert(output.result.tag == (ap_uint<8>)source_cycle);
                for(int col=0; col<fsa::SA_COLS; ++col){
                    const fsa::acc_t expected = scale[(std::size_t)col]
                        *(fsa::acc_t)(col+1)
                        +(fsa::acc_t)(source_cycle-col);
                    assert(fpEqual(
                        output.result.data[(std::size_t)col], expected
                    ));
                }
            }
        }
    }

    // SET_SCALE在接受后立即提交，下一tick的ACC读取新scale。
    const fsa::AccVector new_scale = {{
        (fsa::acc_t)5.0F,
        (fsa::acc_t)6.0F,
        (fsa::acc_t)7.0F,
        (fsa::acc_t)8.0F
    }};
    setScale(state, new_scale);
    fsa::AccumulatorToken acc = command(fsa::AccumulatorCmd::ACC);
    acc.sram_in = {{1.0F, 1.0F, 1.0F, 1.0F}};
    assert(tick(state, acc).input_ready);
    expectVector(waitForResult(state).result.data, new_scale);

    // EXP_S1提交前阻塞后续scale依赖，提交后EXP_S2才可接受。
    fsa::AccumulatorToken exp_s1 = command(fsa::AccumulatorCmd::EXP_S1);
    exp_s1.sa_in = {{-1.0F, -2.0F, -3.0F, -4.0F}};
    assert(tick(state, exp_s1).input_ready);
    fsa::AccumulatorToken exp_s2 = command(fsa::AccumulatorCmd::EXP_S2);
    fsa::AccumulatorPipelineOutput output = tick(state, exp_s2);
    assert(!output.input_ready);

    waitUntilReady(state);
    output = tick(state, exp_s2);
    assert(output.input_ready);
    assert(output.scale_busy);
    while(!output.slow_done){
        output = tick(state);
        assert(!output.input_ready);
    }
    assert(!output.scale_busy);

    waitUntilReady(state);
    acc = command(fsa::AccumulatorCmd::ACC);
    acc.sram_in = {{1.0F, 1.0F, 1.0F, 1.0F}};
    tick(state, acc);
    output = waitForResult(state);
    for(int col=0; col<fsa::SA_COLS; ++col){
        const fsa::acc_t exp_s1_value =
            exp_s1.sa_in[(std::size_t)col]*fsa::attentionScale();
        const fsa::acc_t expected =
            (fsa::acc_t)std::exp2((double)exp_s1_value);
        assert(fpEqual(
            output.result.data[(std::size_t)col],
            expected,
            (fsa::acc_t)1.0e-3F
        ));
    }

    // RECIPROCAL只接受一个启动token，运行期间背压，完成后ACC读新scale。
    const fsa::AccVector denominator = {{1.0F, 2.0F, 3.0F, -4.0F}};
    setScale(state, denominator);
    fsa::AccumulatorToken reciprocal =
        command(fsa::AccumulatorCmd::RECIPROCAL);
    output = tick(state, reciprocal);
    assert(output.input_ready && output.scale_busy);

    acc = command(fsa::AccumulatorCmd::ACC);
    acc.sram_in = {{1.0F, 1.0F, 1.0F, 1.0F}};
    for(int cycle=1; !output.slow_done; ++cycle){
        output = tick(state, acc);
        assert(!output.input_ready);
        assert(cycle <= fsa::reciprocalLatency);
    }
    assert(!output.scale_busy);

    waitUntilReady(state);
    tick(state, acc);
    output = waitForResult(state);
    const fsa::AccVector reciprocal_expected = {{
        1.0F, 0.5F, (fsa::acc_t)(1.0F/3.0F), -0.25F
    }};
    expectVector(
        output.result.data,
        reciprocal_expected,
        (fsa::acc_t)1.0e-6F
    );

    // 慢操作运行中reset必须取消busy、除法状态和全部在途valid。
    setScale(state, denominator);
    tick(state, reciprocal);
    tick(state);
    fsa::reset_accumulator_pipeline_state(state);
    output = tick(state);
    assert(output.input_ready);
    assert(!output.scale_busy);
    assert(!output.slow_done);
    assert(!output.result.valid);

    std::cout << "[PASS] test_accumulator_pipeline: fast II=1 token order, "
                 "metadata, bubbles, scale hazards, EXP_S2, RECIPROCAL, reset"
              << std::endl;
    return 0;
}
