/**
 * @file test_fsa_stream_request_top.cpp
 * @brief 用9x4 Q/K/V验证流式FSA顶层的完整多tile计算。
 */

#include <cmath>
#include <iostream>

#include "fsa/hls/fsa_stream_request_top.hpp"

#if FSA_SA_ROWS!=4 || FSA_SA_COLS!=4

int main(){
    std::cout << "[SKIP] complete 9x4 stream test requires FSA 4x4"
              << std::endl;
    return 0;
}

#else

namespace{

    constexpr int SEQUENCE_LENGTH = 9;
    constexpr int HEAD_DIMENSION = 4;
    constexpr float L_TOLERANCE = 0.02F;
    constexpr float O_TOLERANCE = 0.01F;

    int failure_count = 0;
    float maximum_l_error = 0.0F;
    float maximum_o_error = 0.0F;

    static_assert(
        fsa::SA_ROWS==HEAD_DIMENSION && fsa::SA_COLS==4,
        "9x4 stream request test requires a 4x4 array"
    );

    float qValue(const int query, const int feature){
        return (float)(((query*3+feature*2)%7)-3)*0.125F;
    }

    float kValue(const int key, const int feature){
        return (float)(((key*2+feature*3)%9)-4)*0.125F;
    }

    float vValue(const int key, const int feature){
        return (float)(((key*5+feature*2)%11)-5)*0.25F;
    }

    struct GoldenPrefix{
        float l = 0.0F;
        float unnormalized[HEAD_DIMENSION]{};
        float normalized[HEAD_DIMENSION]{};
    };

    GoldenPrefix calculateGoldenPrefix(
        const int query,
        const int key_count
    ){
        float scores[SEQUENCE_LENGTH]{};
        float maximum = -1.0e30F;
        for(int key=0; key<key_count; ++key){
            float score = 0.0F;
            for(int feature=0; feature<HEAD_DIMENSION; ++feature){
                score += qValue(query, feature)*kValue(key, feature);
            }
            scores[key] = score;
            maximum = score>maximum ? score : maximum;
        }

        GoldenPrefix golden{};
        for(int key=0; key<key_count; ++key){
            const float probability = std::exp(
                (scores[key]-maximum)/
                std::sqrt((float)HEAD_DIMENSION)
            );
            golden.l += probability;
            for(int feature=0; feature<HEAD_DIMENSION; ++feature){
                golden.unnormalized[feature] +=
                    probability*vValue(key, feature);
            }
        }
        for(int feature=0; feature<HEAD_DIMENSION; ++feature){
            golden.normalized[feature] =
                golden.unnormalized[feature]/golden.l;
        }
        return golden;
    }

    void expect(const bool condition, const char* message){
        if(!condition){
            std::cerr << "[FAIL] " << message << std::endl;
            ++failure_count;
        }
    }

    void expectNear(
        const char* field,
        const int query,
        const int feature,
        const int key_count,
        const float actual,
        const float expected,
        const float tolerance
    ){
        const float error = std::fabs(actual-expected);
        if(feature<0){
            maximum_l_error = error>maximum_l_error
                ? error : maximum_l_error;
        }else{
            maximum_o_error = error>maximum_o_error
                ? error : maximum_o_error;
        }
        if(error>tolerance){
            std::cerr << "[FAIL] " << field
                      << " query=" << query
                      << " feature=" << feature
                      << " keys=" << key_count
                      << " actual=" << actual
                      << " expected=" << expected
                      << " error=" << error
                      << " tolerance=" << tolerance
                      << std::endl;
            ++failure_count;
        }
    }

    fsa::FsaCoreRequestInput makeRequest(
        const int query_base,
        const int key_base,
        const int active_keys,
        const bool initialize,
        const bool finalize
    ){
        fsa::FsaCoreRequestInput input{};
        input.request_valid = true;
        input.initialize = initialize;
        input.finalize = finalize;
        input.causal = false;
        input.active_keys = (std::uint16_t)active_keys;
        input.query_base = (std::uint32_t)query_base;
        input.key_base = (std::uint32_t)key_base;

        for(int lane=0; lane<fsa::SA_COLS; ++lane){
            const int query = query_base+lane;
            for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                input.q[lane][feature] = query<SEQUENCE_LENGTH
                    ? (fsa::elem_t)qValue(query, feature)
                    : (fsa::elem_t)0.0F;
            }
        }
        for(int lane=0; lane<fsa::SA_COLS; ++lane){
            const int key = key_base+lane;
            for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                const bool valid = lane<active_keys &&
                    key<SEQUENCE_LENGTH;
                input.k[lane][feature] = valid
                    ? (fsa::elem_t)kValue(key, feature)
                    : (fsa::elem_t)0.0F;
                input.v[lane][feature] = valid
                    ? (fsa::elem_t)vValue(key, feature)
                    : (fsa::elem_t)0.0F;
            }
        }
        return input;
    }

}  // namespace

int main(){
    // reset-only调用验证显式复位，同时为后续RTL cosim建立确定初态。
    fsa::FsaCoreRequestInput reset{};
    reset.reset = true;
    fsa::FsaCoreRequestOutput output{};
    fsa_stream_request_top(reset, output);
    expect(output.request_ready, "reset must leave the request port ready");
    expect(!output.request_done, "reset-only transaction must not execute");
    expect(!output.protocol_error, "reset-only transaction must be legal");

    float final_o[SEQUENCE_LENGTH][HEAD_DIMENSION]{};
    for(int query_block=0; query_block<3; ++query_block){
        const int query_base = query_block*fsa::SA_COLS;

        for(int key_block=0; key_block<3; ++key_block){
            const int key_base = key_block*fsa::SA_COLS;
            const int active_keys = key_block<2 ? 4 : 1;
            const bool initialize = key_block==0;
            const bool finalize = key_block==2;
            const int prefix_keys = key_block<2
                ? (key_block+1)*4 : SEQUENCE_LENGTH;

            const fsa::FsaCoreRequestInput input = makeRequest(
                query_base, key_base, active_keys,
                initialize, finalize
            );
            output = fsa::FsaCoreRequestOutput{};
            fsa_stream_request_top(input, output);

            expect(output.request_ready, "request_ready must stay asserted");
            expect(output.request_done, "every Q/K/V tile must complete");
            expect(!output.protocol_error, "valid tile sequence rejected");
            expect(
                output.normalized==finalize,
                "normalized must match finalize"
            );

            for(int lane=0; lane<fsa::SA_COLS; ++lane){
                const int query = query_base+lane;
                if(query>=SEQUENCE_LENGTH){
                    continue;
                }
                const GoldenPrefix golden = calculateGoldenPrefix(
                    query, prefix_keys
                );
                expectNear(
                    "L", query, -1, prefix_keys,
                    output.l[lane], golden.l, L_TOLERANCE
                );
                for(int feature=0; feature<HEAD_DIMENSION; ++feature){
                    const float expected = finalize
                        ? golden.normalized[feature]
                        : golden.unnormalized[feature];
                    expectNear(
                        finalize ? "normalized O" : "online O",
                        query, feature, prefix_keys,
                        output.o[lane][feature], expected, O_TOLERANCE
                    );
                    if(finalize){
                        final_o[query][feature] = output.o[lane][feature];
                    }
                }
            }
        }
    }

    // 单独再次检查9x4最终结果，确保36个有效输出都被写入。
    for(int query=0; query<SEQUENCE_LENGTH; ++query){
        const GoldenPrefix golden = calculateGoldenPrefix(
            query, SEQUENCE_LENGTH
        );
        for(int feature=0; feature<HEAD_DIMENSION; ++feature){
            expectNear(
                "final 9x4 O", query, feature, SEQUENCE_LENGTH,
                final_o[query][feature], golden.normalized[feature],
                O_TOLERANCE
            );
        }
    }

    if(failure_count!=0){
        std::cerr << "[FAIL] test_fsa_stream_request_top: "
                  << failure_count << " checks failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_fsa_stream_request_top: reset + 9 requests, "
                 "complete 9x4 Q/K/V, online L/O and final O; max L error="
              << maximum_l_error << ", max O error=" << maximum_o_error
              << std::endl;
    return 0;
}

#endif
