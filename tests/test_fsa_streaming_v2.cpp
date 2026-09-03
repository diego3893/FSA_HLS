/**
 * @file test_fsa_streaming_v2.cpp
 * @brief 验证一次调用完成9x4完整attention以及边界保护。
 */

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include "fsa/dma.hpp"
#include "fsa/streaming_v2.hpp"

#ifdef FSA_STREAMING_V2_HLS_TOP
#include "fsa/hls/fsa_streaming_v2_top.hpp"
#endif

namespace{

    constexpr int D = fsa::SA_ROWS;
    constexpr int TILE = fsa::SA_COLS;
    constexpr int L = 2*TILE+1;
    constexpr int OUTPUT_WORDS = L*fsa::DMA_O_WORDS_PER_ROW;

    static_assert(TILE<=D, "v2要求SA_COLS不大于SA_ROWS");
    static_assert(L<=fsa::MAX_SEQUENCE_LENGTH, "测试序列超过配置上限");

    float Q[L][D]{};
    float K[L][D]{};
    float V[L][D]{};
    fsa::dma_word_t q_memory[fsa::DMA_MAX_QKV_WORDS]{};
    fsa::dma_word_t k_memory[fsa::DMA_MAX_QKV_WORDS]{};
    fsa::dma_word_t v_memory[fsa::DMA_MAX_QKV_WORDS]{};
    fsa::dma_word_t o_memory[fsa::DMA_MAX_O_WORDS+2]{};
    int failures = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            std::cerr << "[FAIL] " << message << std::endl;
            ++failures;
        }
    }

    void initializeMatrices(){
        for(int token=0; token<L; ++token){
            for(int feature=0; feature<D; ++feature){
                Q[token][feature] =
                    (float)(((token+2*feature)%5)-2)*0.25F;
                K[token][feature] =
                    (float)(((2*token+feature)%7)-3)*0.25F;
                V[token][feature] =
                    (float)(((token+feature)%6)-2)*0.5F;
            }
        }
    }

    void packMatrix(
        const float source[L][D],
        fsa::dma_word_t destination[fsa::DMA_MAX_QKV_WORDS]
    ){
        for(int token=0; token<L; ++token){
            for(int word=0; word<fsa::DMA_QKV_WORDS_PER_ROW; ++word){
                fsa::elem_t values[fsa::DMA_ELEMS_PER_WORD]{};
                for(int lane=0; lane<fsa::DMA_ELEMS_PER_WORD; ++lane){
                    values[lane] = (fsa::elem_t)source[token]
                        [word*fsa::DMA_ELEMS_PER_WORD+lane];
                }
                destination[token*fsa::DMA_QKV_WORDS_PER_ROW+word] =
                    fsa::dma_pack_elem_word(values);
            }
        }
    }

    float readOutput(const int token, const int feature){
        const int word = token*fsa::DMA_O_WORDS_PER_ROW+
            feature/fsa::DMA_ACCS_PER_WORD;
        const int lane = feature%fsa::DMA_ACCS_PER_WORD;
        return (float)fsa::dma_unpack_acc(o_memory[word], lane);
    }

    void golden(const bool causal, float expected[L][D]){
        for(int query=0; query<L; ++query){
            float scores[L]{};
            float row_max = -1.0e30F;
            const int key_count = causal ? query+1 : L;
            for(int key=0; key<key_count; ++key){
                for(int feature=0; feature<D; ++feature){
                    scores[key] += Q[query][feature]*K[key][feature];
                }
                if(scores[key]>row_max){
                    row_max = scores[key];
                }
            }

            float row_sum = 0.0F;
            for(int key=0; key<key_count; ++key){
                const float probability = std::exp(
                    (scores[key]-row_max)/std::sqrt((float)D)
                );
                row_sum += probability;
                for(int feature=0; feature<D; ++feature){
                    expected[query][feature] +=
                        probability*V[key][feature];
                }
            }
            for(int feature=0; feature<D; ++feature){
                expected[query][feature] /= row_sum;
            }
        }
    }

    void callDut(const bool causal, ap_uint<8>& status){
#ifdef FSA_STREAMING_V2_HLS_TOP
        fsa_streaming_v2_top(
            q_memory, k_memory, v_memory, o_memory,
            (ap_uint<32>)L, causal, status
        );
#else
        fsa::fsa_streaming_v2_run(
            q_memory, k_memory, v_memory, o_memory,
            (ap_uint<32>)L, causal, status
        );
#endif
    }

    void runAttentionCase(const bool causal){
        const fsa::dma_word_t canary =
            (fsa::dma_word_t)0x5a5aa5a55a5aa5a5ULL;
        for(int word=0; word<fsa::DMA_MAX_O_WORDS+2; ++word){
            o_memory[word] = canary;
        }

        ap_uint<8> status = 0xff;
        callDut(causal, status);
        expect(
            status==(ap_uint<8>)static_cast<std::uint8_t>(
                fsa::FsaStreamingV2Status::OK
            ),
            "v2 top returned an error status"
        );
        expect(
            o_memory[OUTPUT_WORDS]==canary &&
                o_memory[OUTPUT_WORDS+1]==canary,
            "O DMA wrote beyond the complete output matrix"
        );

        float expected[L][D]{};
        golden(causal, expected);
        for(int query=0; query<L; ++query){
            for(int feature=0; feature<D; ++feature){
                const float actual = readOutput(query, feature);
                expect(
                    std::isfinite(actual) &&
                        std::fabs(actual-expected[query][feature])<=0.18F,
                    std::string(causal ? "causal" : "non-causal")+
                        " O mismatch at query "+std::to_string(query)+
                        ", feature "+std::to_string(feature)
                );
            }
        }
    }

    void runInvalidLengthCase(){
        const fsa::dma_word_t canary =
            (fsa::dma_word_t)0x6b6bb4b46b6bb4b4ULL;
        o_memory[0] = canary;
        ap_uint<8> status = 0xff;
#ifdef FSA_STREAMING_V2_HLS_TOP
        fsa_streaming_v2_top(
            q_memory, k_memory, v_memory, o_memory,
            (ap_uint<32>)0, false, status
        );
#else
        fsa::fsa_streaming_v2_run(
            q_memory, k_memory, v_memory, o_memory,
            (ap_uint<32>)0, false, status
        );
#endif
        expect(
            status==(ap_uint<8>)static_cast<std::uint8_t>(
                fsa::FsaStreamingV2Status::INVALID_SEQUENCE_LENGTH
            ),
            "zero length was not rejected"
        );
        expect(o_memory[0]==canary, "invalid request modified O memory");
    }

}  // namespace

int main(){
    initializeMatrices();
    packMatrix(Q, q_memory);
    packMatrix(K, k_memory);
    packMatrix(V, v_memory);

    runAttentionCase(false);
    runAttentionCase(true);
    runInvalidLengthCase();

    if(failures!=0){
        std::cerr << "[FAIL] test_fsa_streaming_v2: "
                  << failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_fsa_streaming_v2: one call produced complete "
              << L << "x" << D
              << " causal and non-causal outputs" << std::endl;
    return 0;
}
