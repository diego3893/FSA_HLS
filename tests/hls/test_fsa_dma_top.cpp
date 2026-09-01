/**
 * @file test_fsa_dma_top.cpp
 * @brief 验证一次启动完成非整tile的L x head_dim完整attention
 */

#include <cmath>
#include <iostream>
#include <string>

#include "fsa/dma.hpp"
#include "fsa/hls/fsa_dma_top.hpp"

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
    float sqrt(const float value){ return std::sqrt(value); }
}
#endif

namespace{

    constexpr int D = fsa::SA_ROWS;
    constexpr int TILE = fsa::SA_COLS;
    constexpr int L = 2*TILE+1;
    constexpr int O_WORDS = L*fsa::DMA_O_WORDS_PER_ROW;

    static_assert(TILE<=D, "test要求SA_COLS不大于SA_ROWS");
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

    float readOutput(
        const fsa::dma_word_t memory[fsa::DMA_MAX_O_WORDS+2],
        const int token,
        const int feature
    ){
        const int word = token*fsa::DMA_O_WORDS_PER_ROW+
            feature/fsa::DMA_ACCS_PER_WORD;
        const int lane = feature%fsa::DMA_ACCS_PER_WORD;
        return (float)fsa::dma_unpack_acc(memory[word], lane);
    }

    void golden(float expected[L][D], const bool causal){
        for(int query=0; query<L; ++query){
            float scores[L]{};
            float row_max = -1.0e30F;
            for(int key=0; key<L; ++key){
                if(causal && key>query){
                    continue;
                }
                for(int feature=0; feature<D; ++feature){
                    scores[key] += Q[query][feature]*K[key][feature];
                }
                if(scores[key]>row_max){
                    row_max = scores[key];
                }
            }

            float row_sum = 0.0F;
            for(int key=0; key<L; ++key){
                if(causal && key>query){
                    continue;
                }
                const float probability = std::exp(
                    (scores[key]-row_max)/std::sqrt((float)D)
                );
                row_sum += probability;
                for(int feature=0; feature<D; ++feature){
                    expected[query][feature] += probability*V[key][feature];
                }
            }
            for(int feature=0; feature<D; ++feature){
                expected[query][feature] /= row_sum;
            }
        }
    }

}  // namespace

int main(){
    initializeMatrices();

    packMatrix(Q, q_memory);
    packMatrix(K, k_memory);
    packMatrix(V, v_memory);

    const fsa::dma_word_t canary =
        (fsa::dma_word_t)0x5a5aa5a55a5aa5a5ULL;
    for(int word=0; word<fsa::DMA_MAX_O_WORDS+2; ++word){
        o_memory[word] = canary;
    }

    ap_uint<8> status = 0xff;
    fsa_dma_top(
        q_memory,
        k_memory,
        v_memory,
        o_memory,
        (ap_uint<32>)0,
        false,
        status
    );
    expect(
        status==(ap_uint<8>)static_cast<std::uint8_t>(
            fsa::FsaDmaStatus::INVALID_SEQUENCE_LENGTH
        ),
        "zero sequence length was not rejected"
    );

    status = 0xff;
    fsa_dma_top(
        q_memory,
        k_memory,
        v_memory,
        o_memory,
        (ap_uint<32>)L,
        false,
        status
    );

    expect(
        status==(ap_uint<8>)static_cast<std::uint8_t>(
            fsa::FsaDmaStatus::OK
        ),
        "DMA top returned an error status"
    );
    expect(
        o_memory[O_WORDS]==canary && o_memory[O_WORDS+1]==canary,
        "O DMA wrote beyond the complete output matrix"
    );

    float expected[L][D]{};
    golden(expected, false);
    for(int query=0; query<L; ++query){
        for(int feature=0; feature<D; ++feature){
            const float actual = readOutput(o_memory, query, feature);
            expect(
                std::isfinite(actual) &&
                    std::fabs(actual-expected[query][feature])<=0.18F,
                "O mismatch at query "+std::to_string(query)+
                    ", feature "+std::to_string(feature)
            );
        }
    }

    for(int word=0; word<fsa::DMA_MAX_O_WORDS+2; ++word){
        o_memory[word] = canary;
    }
    status = 0xff;
    fsa_dma_top(
        q_memory,
        k_memory,
        v_memory,
        o_memory,
        (ap_uint<32>)L,
        true,
        status
    );
    expect(
        status==(ap_uint<8>)static_cast<std::uint8_t>(
            fsa::FsaDmaStatus::OK
        ),
        "causal DMA top returned an error status"
    );
    float causal_expected[L][D]{};
    golden(causal_expected, true);
    for(int query=0; query<L; ++query){
        for(int feature=0; feature<D; ++feature){
            const float actual = readOutput(o_memory, query, feature);
            expect(
                std::isfinite(actual) &&
                    std::fabs(actual-causal_expected[query][feature])<=0.18F,
                "causal O mismatch at query "+std::to_string(query)+
                    ", feature "+std::to_string(feature)
            );
        }
    }

    if(failures!=0){
        std::cerr << "[FAIL] test_fsa_dma_top: "
                  << failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_fsa_dma_top: non-causal and causal "
              << L << "x" << D << " O with " << TILE
              << "-token tiles" << std::endl;
    return 0;
}
