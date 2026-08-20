/**
 * @file test_fsa_core_execute_top.cpp
 * @brief 用五次指令事务完成4x4 FlashAttention，不再由testbench逐step发控制
 */

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "fsa/hls/fsa_core_execute_top.hpp"

#ifdef FSA_LOCAL_MATH_STUBS
namespace hls{
    float fabs(const float value){ return std::fabs(value); }
    float fma(const float a, const float b, const float c){
        return std::fma(a, b, c);
    }
    float trunc(const float value){ return std::trunc(value); }
    float ldexp(const float value, const int exponent){
        return std::ldexp(value, exponent);
    }
}
#endif

namespace{

    constexpr int N = 4;
    constexpr int Q_BASE_ADDRESS = 0;
    constexpr int K_BASE_ADDRESS = Q_BASE_ADDRESS+N;
    constexpr int VT_BASE_ADDRESS = K_BASE_ADDRESS+N;
    constexpr int L_ADDRESS = 0;
    constexpr int O_BASE_ADDRESS = 1;

    static_assert(fsa::SA_ROWS==N && fsa::SA_COLS==N,
                  "test_fsa_core_execute_top requires a 4x4 SA");
    static_assert(fsa::SPAD_SUB_BANKS==1,
                  "test assumes one Scratchpad sub-bank");
    static_assert(fsa::ACC_SUB_BANKS==2,
                  "test assumes two accumulator sub-banks");

    const float Q[N][N] = {
        {1, 2, 3, 4},
        {2, 1, 0, 1},
        {1, 0, 1, 0},
        {3, 2, 1, 0}
    };
    const float K[N][N] = {
        {1, 0, 2, 1},
        {0, 1, 1, 2},
        {2, 1, 0, 1},
        {1, 2, 1, 0}
    };
    const float V[N][N] = {
        {1, 2, 3, 4},
        {2, 0, 1, 3},
        {0, 1, 2, 1},
        {3, 2, 0, 1}
    };

    int failure_count = 0;
    int transaction_count = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            std::cerr << "[FAIL] transaction " << transaction_count
                      << ": " << message << std::endl;
            ++failure_count;
        }
    }

    fsa::FsaCoreExecuteOutput call(
        const fsa::FsaCoreExecuteInput& input
    ){
        fsa::FsaCoreExecuteOutput output{};
        fsa_core_execute_top(input, output);
        ++transaction_count;
        return output;
    }

    void resetCore(){
        fsa::FsaCoreExecuteInput input{};
        input.reset = true;
        const fsa::FsaCoreExecuteOutput output = call(input);
        expect(!output.instruction_done, "reset reported instruction_done");
    }

    void writeSpadRow(const int address, const float row[N]){
        constexpr int ELEMENTS_PER_SUB_BANK =
            fsa::SA_ROWS/fsa::SPAD_SUB_BANKS;
        for(int sub_bank=0; sub_bank<fsa::SPAD_SUB_BANKS; ++sub_bank){
            fsa::FsaCoreExecuteInput input{};
            input.spad_write_valid[0] = true;
            input.spad_write_addr[0] = address;
            input.spad_write_sub_bank[0] = sub_bank;
            for(int element=0; element<ELEMENTS_PER_SUB_BANK; ++element){
                const int row_element =
                    sub_bank*ELEMENTS_PER_SUB_BANK+element;
                input.spad_write_data[0][element] =
                    (fsa::elem_t)row[row_element];
            }
            const fsa::FsaCoreExecuteOutput output = call(input);
            expect(output.spad_write_ready[0], "spad preload backpressured");
        }
    }

    void preloadQKV(){
        for(int row=0; row<N; ++row){
            writeSpadRow(Q_BASE_ADDRESS+row, Q[row]);
        }
        for(int row=0; row<N; ++row){
            writeSpadRow(K_BASE_ADDRESS+row, K[row]);
        }
        for(int output_feature=0; output_feature<N; ++output_feature){
            float transposed[N]{};
            for(int key=0; key<N; ++key){
                transposed[key] = V[key][output_feature];
            }
            writeSpadRow(VT_BASE_ADDRESS+output_feature, transposed);
        }
    }

    fsa::MatrixInstruction baseInstruction(const fsa::MxFunc function){
        fsa::MatrixInstruction instruction{};
        instruction.header.func = function;
        instruction.spad.stride = 1;
        instruction.acc.stride = 1;
        return instruction;
    }

    void execute(
        const fsa::MatrixInstruction& instruction,
        const unsigned expected_steps,
        const char* name
    ){
        fsa::FsaCoreExecuteInput input{};
        input.instruction_valid = true;
        input.instruction = instruction;
        const fsa::FsaCoreExecuteOutput output = call(input);
        expect(output.instruction_done, std::string(name)+" did not finish");
        expect(!output.busy, std::string(name)+" stayed busy");
        expect(
            output.executed_steps.to_uint()==expected_steps,
            std::string(name)+" executed wrong step count"
        );
    }

    void runUninterruptedPlan(){
        fsa::MatrixInstruction load =
            baseInstruction(fsa::MxFunc::LOAD_STATIONARY);
        load.spad.addr = Q_BASE_ADDRESS+N-1;
        load.spad.stride = -1;
        execute(load, 5, "LOAD_STATIONARY");

        fsa::MatrixInstruction score =
            baseInstruction(fsa::MxFunc::ATTENTION_SCORE_COMPUTE);
        score.spad.addr = K_BASE_ADDRESS;
        score.spad.revInput = true;
        score.spad.delayOutput = true;
        score.spad.revOutput = true;
        score.acc.addr = L_ADDRESS;
        score.acc.zero = true;
        execute(score, 28, "ATTENTION_SCORE_COMPUTE");

        fsa::MatrixInstruction value =
            baseInstruction(fsa::MxFunc::ATTENTION_VALUE_COMPUTE);
        value.spad.addr = VT_BASE_ADDRESS;
        value.spad.revInput = true;
        value.spad.delayOutput = true;
        value.spad.revOutput = false;
        value.acc.addr = O_BASE_ADDRESS;
        value.acc.zero = true;
        execute(value, 12, "ATTENTION_VALUE_COMPUTE");

        fsa::MatrixInstruction scale =
            baseInstruction(fsa::MxFunc::ATTENTION_LSE_NORM_SCALE);
        scale.acc.addr = L_ADDRESS;
        execute(scale, 17, "ATTENTION_LSE_NORM_SCALE");

        fsa::MatrixInstruction norm =
            baseInstruction(fsa::MxFunc::ATTENTION_LSE_NORM);
        norm.acc.addr = O_BASE_ADDRESS;
        execute(norm, 5, "ATTENTION_LSE_NORM");
    }

    fsa::AccVector readAccRow(const int address){
        fsa::FsaCoreExecuteInput request{};
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            request.acc_dma_read_valid[sub_bank] = true;
            request.acc_dma_read_addr[sub_bank] = address;
            request.acc_dma_read_sub_bank[sub_bank] = sub_bank;
        }
        const fsa::FsaCoreExecuteOutput request_output = call(request);
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            expect(
                request_output.acc_dma_read_ready[sub_bank],
                "accRAM read request backpressured"
            );
        }

        const fsa::FsaCoreExecuteOutput response =
            call(fsa::FsaCoreExecuteInput{});
        fsa::AccVector row{};
        constexpr int ELEMENTS_PER_SUB_BANK =
            fsa::SA_COLS/fsa::ACC_SUB_BANKS;
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            expect(
                response.acc_dma_response_valid[sub_bank],
                "accRAM response-valid missing"
            );
            for(int element=0; element<ELEMENTS_PER_SUB_BANK; ++element){
                row[(std::size_t)(sub_bank*ELEMENTS_PER_SUB_BANK+element)] =
                    response.acc_dma_read_data[sub_bank][element];
            }
        }
        return row;
    }

    void calculateGolden(float expected_l[N], float expected_o[N][N]){
        for(int query=0; query<N; ++query){
            float score[N]{};
            float rowmax = -1.0e30F;
            for(int key=0; key<N; ++key){
                for(int feature=0; feature<N; ++feature){
                    score[key] += Q[query][feature]*K[key][feature];
                }
                rowmax = score[key]>rowmax ? score[key] : rowmax;
            }

            float probability[N]{};
            for(int key=0; key<N; ++key){
                probability[key] =
                    std::exp((score[key]-rowmax)/std::sqrt((float)N));
                expected_l[query] += probability[key];
            }

            for(int output_feature=0; output_feature<N; ++output_feature){
                float numerator = 0.0F;
                for(int key=0; key<N; ++key){
                    numerator += probability[key]*V[key][output_feature];
                }
                expected_o[query][output_feature] =
                    numerator/expected_l[query];
            }
        }
    }

    void checkVector(
        const char* name,
        const fsa::AccVector& actual,
        const float expected[N],
        const float tolerance
    ){
        for(int lane=0; lane<N; ++lane){
            const bool match = std::isfinite(actual[(std::size_t)lane]) &&
                std::fabs(actual[(std::size_t)lane]-expected[lane])<=tolerance;
            expect(
                match,
                std::string(name)+" lane "+std::to_string(lane)+
                    " expected "+std::to_string(expected[lane])+
                    ", actual "+
                    std::to_string(actual[(std::size_t)lane])
            );
        }
    }

}  // namespace

int main(){
    float expected_l[N]{};
    float expected_o[N][N]{};
    calculateGolden(expected_l, expected_o);

    resetCore();
    preloadQKV();
    runUninterruptedPlan();

    checkVector("L", readAccRow(L_ADDRESS), expected_l, 0.08F);
    for(int output_feature=0; output_feature<N; ++output_feature){
        float expected_row[N]{};
        for(int query=0; query<N; ++query){
            expected_row[query] = expected_o[query][output_feature];
        }
        checkVector(
            "O",
            readAccRow(O_BASE_ADDRESS+output_feature),
            expected_row,
            0.08F
        );
    }

    if(failure_count!=0){
        std::cerr << "[FAIL] test_fsa_core_execute_top: "
                  << failure_count << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_fsa_core_execute_top: five instruction "
                 "transactions completed full 4x4 FA" << std::endl;
    return 0;
}
