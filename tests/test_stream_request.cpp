/**
 * @file test_stream_request.cpp
 * @brief 流式FSA请求核的跨tile在线softmax和协议测试。
 */

#include <cassert>
#include <cmath>
#include <iostream>

#include "fsa/stream_array.hpp"

namespace{

    constexpr int QN = fsa::SA_COLS;
    constexpr int D = fsa::SA_ROWS;
    constexpr int BLOCKS = 2;
    constexpr int KEYS = BLOCKS*QN;

    float q[QN][D]{};
    float k[BLOCKS][QN][D]{};
    float v[BLOCKS][QN][D]{};

    unsigned expectedSteps(const bool initialize_state, const bool finalize){
        const unsigned reset_steps = initialize_state ? QN : 0;
        const unsigned preload_steps =
            (QN+2*D)*fsa::SPAD_SUB_BANKS;
        const unsigned instruction_steps =
            (QN+1)+
            ((2*D+4+fsa::exp2PWLPieces-1)+D+QN+1)+
            (D+QN+D);
        const unsigned normalization_steps = finalize
            ? (2+fsa::reciprocalLatency)+(D+1)
            : 0;
        return reset_steps+preload_steps+instruction_steps+
            normalization_steps+2*(1+D);
    }

    void initialize(){
        for(int query=0; query<QN; ++query){
            for(int feature=0; feature<D; ++feature){
                q[query][feature] =
                    (float)(((query+2*feature)%5)-2)*0.25F;
            }
        }
        for(int block=0; block<BLOCKS; ++block){
            for(int key=0; key<QN; ++key){
                for(int feature=0; feature<D; ++feature){
                    k[block][key][feature] =
                        (float)(((2*(block*QN+key)+feature)%7)-3)*0.25F;
                    v[block][key][feature] =
                        (float)((((block+1)*key+feature)%6)-2)*0.5F;
                }
            }
        }
    }

    fsa::FsaCoreRequestOutput run(
        const int block,
        const bool initialize_state,
        const bool finalize
    ){
        fsa::FsaCoreRequestInput input{};
        input.request_valid = true;
        input.initialize = initialize_state;
        input.finalize = finalize;
        input.active_keys = QN;
        input.key_base = block*QN;
        for(int query=0; query<QN; ++query){
            for(int feature=0; feature<D; ++feature){
                input.q[query][feature] = (fsa::elem_t)q[query][feature];
            }
        }
        for(int key=0; key<QN; ++key){
            for(int feature=0; feature<D; ++feature){
                input.k[key][feature] =
                    (fsa::elem_t)k[block][key][feature];
                input.v[key][feature] =
                    (fsa::elem_t)v[block][key][feature];
            }
        }
        fsa::FsaCoreRequestOutput output{};
        fsa::fsa_stream_request_run(input, output);
        return output;
    }

    void golden(float output[QN][D]){
        for(int query=0; query<QN; ++query){
            float score[KEYS]{};
            float maximum = -1.0e30F;
            for(int key=0; key<KEYS; ++key){
                const int block = key/QN;
                const int lane = key%QN;
                for(int feature=0; feature<D; ++feature){
                    score[key] += q[query][feature]*
                        k[block][lane][feature];
                }
                maximum = score[key]>maximum ? score[key] : maximum;
            }
            float denominator = 0.0F;
            for(int key=0; key<KEYS; ++key){
                const int block = key/QN;
                const int lane = key%QN;
                const float probability = std::exp(
                    (score[key]-maximum)/std::sqrt((float)D)
                );
                denominator += probability;
                for(int feature=0; feature<D; ++feature){
                    output[query][feature] +=
                        probability*v[block][lane][feature];
                }
            }
            for(int feature=0; feature<D; ++feature){
                output[query][feature] /= denominator;
            }
        }
    }

}  // namespace

int main(){
    initialize();

    fsa::FsaCoreRequestInput reset{};
    reset.reset = true;
    fsa::FsaCoreRequestOutput reset_output{};
    fsa::fsa_stream_request_run(reset, reset_output);
    assert(reset_output.request_ready);

    const fsa::FsaCoreRequestOutput first = run(0, true, false);
    assert(first.request_done && !first.protocol_error);
    assert(!first.normalized);
    assert(first.executed_steps.to_uint()==expectedSteps(true, false));

    const fsa::FsaCoreRequestOutput second = run(1, false, true);
    assert(second.request_done && !second.protocol_error);
    assert(second.normalized);
    assert(second.executed_steps.to_uint()==expectedSteps(false, true));

    float expected[QN][D]{};
    golden(expected);
    for(int query=0; query<QN; ++query){
        for(int feature=0; feature<D; ++feature){
            const float error = std::fabs(
                second.o[query][feature]-expected[query][feature]
            );
            if(error>0.18F){
                std::cerr << "query=" << query
                          << ", feature=" << feature
                          << ", actual=" << second.o[query][feature]
                          << ", expected=" << expected[query][feature]
                          << ", error=" << error << std::endl;
            }
            assert(error<=0.18F);
        }
    }

    fsa::FsaCoreRequestInput invalid{};
    invalid.request_valid = true;
    invalid.initialize = false;
    fsa::FsaCoreRequestOutput invalid_output{};
    fsa::fsa_stream_request_run(invalid, invalid_output);
    assert(invalid_output.protocol_error && !invalid_output.request_done);

    // 单tile同时覆盖causal和非满active_keys；padding与未来key都必须
    // 对softmax严格贡献0。
    fsa::FsaCoreRequestInput causal{};
    causal.reset = true;
    causal.request_valid = true;
    causal.initialize = true;
    causal.finalize = true;
    causal.causal = true;
    causal.active_keys = QN-1;
    for(int query=0; query<QN; ++query){
        for(int feature=0; feature<D; ++feature){
            causal.q[query][feature] = (fsa::elem_t)q[query][feature];
        }
    }
    for(int key=0; key<QN; ++key){
        for(int feature=0; feature<D; ++feature){
            causal.k[key][feature] = (fsa::elem_t)k[0][key][feature];
            causal.v[key][feature] = (fsa::elem_t)v[0][key][feature];
        }
    }
    fsa::FsaCoreRequestOutput causal_output{};
    fsa::fsa_stream_request_run(causal, causal_output);
    assert(causal_output.request_done && !causal_output.protocol_error);
    for(int query=0; query<QN; ++query){
        float causal_score[QN]{};
        float maximum = -1.0e30F;
        const int allowed_keys = query+1<QN-1 ? query+1 : QN-1;
        for(int key=0; key<allowed_keys; ++key){
            for(int feature=0; feature<D; ++feature){
                causal_score[key] +=
                    q[query][feature]*k[0][key][feature];
            }
            maximum = causal_score[key]>maximum
                ? causal_score[key] : maximum;
        }
        float denominator = 0.0F;
        float numerator[D]{};
        for(int key=0; key<allowed_keys; ++key){
            const float probability = std::exp(
                (causal_score[key]-maximum)/std::sqrt((float)D)
            );
            denominator += probability;
            for(int feature=0; feature<D; ++feature){
                numerator[feature] += probability*v[0][key][feature];
            }
        }
        for(int feature=0; feature<D; ++feature){
            assert(std::fabs(
                causal_output.o[query][feature]-
                numerator[feature]/denominator
            )<=0.18F);
        }
    }

    std::cout << "[PASS] test_stream_request: online, causal and tail tile"
              << std::endl;
    return 0;
}
