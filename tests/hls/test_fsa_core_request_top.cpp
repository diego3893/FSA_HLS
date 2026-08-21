/**
 * @file test_fsa_core_request_top.cpp
 * @brief 两个KV block验证请求级Controller和非零旧L/O在线softmax
 */

#include <cmath>
#include <iostream>
#include <string>

#include "fsa/hls/fsa_core_request_top.hpp"

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
    constexpr int BLOCKS = 2;
    constexpr int TOTAL_KEYS = BLOCKS*N;

    static_assert(
        fsa::SA_ROWS==N && fsa::SA_COLS==N,
        "test_fsa_core_request_top requires the 4x4 configuration"
    );

    const float Q[N][N] = {
        {1.0F, 0.0F, 1.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, 1.0F},
        {1.0F, 1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F, 1.0F}
    };

    const float K[BLOCKS][N][N] = {
        {
            {1.0F, 0.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 1.0F, 0.0F},
            {0.0F, 0.0F, 0.0F, 1.0F}
        },
        {
            {1.0F, 1.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 1.0F, 0.0F},
            {0.0F, 0.0F, 1.0F, 1.0F},
            {1.0F, 0.0F, 0.0F, 1.0F}
        }
    };

    const float V[BLOCKS][N][N] = {
        {
            {1.0F, 0.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 1.0F, 0.0F},
            {0.0F, 0.0F, 0.0F, 1.0F}
        },
        {
            {1.0F, 2.0F, 0.0F, 1.0F},
            {2.0F, 0.0F, 1.0F, 1.0F},
            {0.0F, 1.0F, 2.0F, 1.0F},
            {1.0F, 1.0F, 1.0F, 2.0F}
        }
    };

    int failures = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            std::cerr << "[FAIL] " << message << std::endl;
            ++failures;
        }
    }

    void resetCore(){
        fsa::FsaCoreRequestInput input{};
        input.reset = true;
        fsa::FsaCoreRequestOutput output{};
        fsa_core_request_top(input, output);
        expect(output.request_ready, "reset did not leave request interface ready");
        expect(!output.request_done, "reset reported a completed request");
    }

    fsa::FsaCoreRequestOutput runBlock(
        const int block,
        const bool initialize,
        const bool finalize
    ){
        fsa::FsaCoreRequestInput input{};
        input.request_valid = true;
        input.initialize = initialize;
        input.finalize = finalize;
        for(int query=0; query<N; ++query){
            for(int feature=0; feature<N; ++feature){
                input.q[query][feature] =
                    (fsa::elem_t)Q[query][feature];
            }
        }
        for(int key=0; key<N; ++key){
            for(int feature=0; feature<N; ++feature){
                input.k[key][feature] =
                    (fsa::elem_t)K[block][key][feature];
                input.v[key][feature] =
                    (fsa::elem_t)V[block][key][feature];
            }
        }

        fsa::FsaCoreRequestOutput output{};
        fsa_core_request_top(input, output);
        expect(output.request_done, "valid request did not complete");
        expect(!output.protocol_error, "valid request reported protocol_error");
        return output;
    }

    void golden(float expected_l[N], float expected_o[N][N]){
        for(int query=0; query<N; ++query){
            float scores[TOTAL_KEYS]{};
            float row_max = -1.0e30F;
            for(int block=0; block<BLOCKS; ++block){
                for(int key=0; key<N; ++key){
                    const int global_key = block*N+key;
                    for(int feature=0; feature<N; ++feature){
                        scores[global_key] +=
                            Q[query][feature]*K[block][key][feature];
                    }
                    if(scores[global_key]>row_max){
                        row_max = scores[global_key];
                    }
                }
            }

            for(int block=0; block<BLOCKS; ++block){
                for(int key=0; key<N; ++key){
                    const int global_key = block*N+key;
                    const float probability = std::exp(
                        (scores[global_key]-row_max)/std::sqrt((float)N)
                    );
                    expected_l[query] += probability;
                    for(int value_feature=0;
                            value_feature<N; ++value_feature){
                        expected_o[query][value_feature] +=
                            probability*V[block][key][value_feature];
                    }
                }
            }
            for(int value_feature=0;
                    value_feature<N; ++value_feature){
                expected_o[query][value_feature] /= expected_l[query];
            }
        }
    }

}  // namespace

int main(){
    resetCore();

    const fsa::FsaCoreRequestOutput first = runBlock(0, true, false);
    expect(!first.normalized, "intermediate block was normalized early");
    expect(
        first.executed_steps.to_uint()==71,
        "first block executed unexpected logical-step count"
    );

    const fsa::FsaCoreRequestOutput second = runBlock(1, false, true);
    expect(second.normalized, "final block did not normalize O");
    expect(
        second.executed_steps.to_uint()==89,
        "final block executed unexpected logical-step count"
    );

    float expected_l[N]{};
    float expected_o[N][N]{};
    golden(expected_l, expected_o);

    for(int query=0; query<N; ++query){
        expect(
            std::isfinite(second.l[query]) &&
                std::fabs(second.l[query]-expected_l[query])<=0.12F,
            "L mismatch at query "+std::to_string(query)
        );
        for(int value_feature=0;
                value_feature<N; ++value_feature){
            expect(
                std::isfinite(second.o[query][value_feature]) &&
                    std::fabs(
                        second.o[query][value_feature]-
                        expected_o[query][value_feature]
                    )<=0.12F,
                "O mismatch at query "+std::to_string(query)+
                ", feature "+std::to_string(value_feature)
            );
        }
    }

    if(failures!=0){
        std::cerr << "[FAIL] test_fsa_core_request_top: "
                  << failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_fsa_core_request_top: two-block online "
                 "softmax reused nonzero L/O and finalized O/L"
              << std::endl;
    return 0;
}
