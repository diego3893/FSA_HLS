/**
 * @file test_fsa_dma_top.cpp
 * @brief 验证64-bit DDR布局、VT搬入、单tile FA和query-major OL写回
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
}
#endif

namespace{

    constexpr int N = 4;

    static_assert(
        fsa::SA_ROWS==N && fsa::SA_COLS==N,
        "test_fsa_dma_top requires the 4x4 configuration"
    );

    const float Q[N][N] = {
        {1.0F, 0.0F, 1.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, 1.0F},
        {1.0F, 1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F, 1.0F}
    };

    const float K[N][N] = {
        {1.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}
    };

    const float V[N][N] = {
        {1.0F, 2.0F, 0.0F, 1.0F},
        {2.0F, 0.0F, 1.0F, 1.0F},
        {0.0F, 1.0F, 2.0F, 1.0F},
        {1.0F, 1.0F, 1.0F, 2.0F}
    };

    int failures = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            std::cerr << "[FAIL] " << message << std::endl;
            ++failures;
        }
    }

    template <int Rows, int Cols>
    void packElemMatrix(
        const float source[Rows][Cols],
        fsa::dma_word_t destination[fsa::DMA_QKV_WORDS]
    ){
        for(int word=0; word<fsa::DMA_QKV_WORDS; ++word){
            fsa::elem_t values[fsa::DMA_ELEMS_PER_WORD]{};
            for(int lane=0; lane<fsa::DMA_ELEMS_PER_WORD; ++lane){
                const int linear = word*fsa::DMA_ELEMS_PER_WORD+lane;
                values[lane] = (fsa::elem_t)source[linear/Cols][linear%Cols];
            }
            destination[word] = fsa::dma_pack_elem_word(values);
        }
    }

    void packTransposedV(
        fsa::dma_word_t destination[fsa::DMA_QKV_WORDS]
    ){
        for(int word=0; word<fsa::DMA_QKV_WORDS; ++word){
            fsa::elem_t values[fsa::DMA_ELEMS_PER_WORD]{};
            for(int lane=0; lane<fsa::DMA_ELEMS_PER_WORD; ++lane){
                const int linear = word*fsa::DMA_ELEMS_PER_WORD+lane;
                const int value_feature = linear/N;
                const int key = linear%N;
                values[lane] = (fsa::elem_t)V[key][value_feature];
            }
            destination[word] = fsa::dma_pack_elem_word(values);
        }
    }

    float readOlValue(
        const fsa::dma_word_t ol[fsa::DMA_OL_WORDS],
        const int index
    ){
        const int word = index/fsa::DMA_ACCS_PER_WORD;
        const int lane = index%fsa::DMA_ACCS_PER_WORD;
        return (float)fsa::dma_unpack_acc(ol[word], lane);
    }

    void golden(float expected_l[N], float expected_o[N][N]){
        for(int query=0; query<N; ++query){
            float scores[N]{};
            float row_max = -1.0e30F;
            for(int key=0; key<N; ++key){
                for(int feature=0; feature<N; ++feature){
                    scores[key] += Q[query][feature]*K[key][feature];
                }
                if(scores[key]>row_max){
                    row_max = scores[key];
                }
            }

            for(int key=0; key<N; ++key){
                const float probability = std::exp(
                    (scores[key]-row_max)/std::sqrt((float)N)
                );
                expected_l[query] += probability;
                for(int value_feature=0;
                        value_feature<N; ++value_feature){
                    expected_o[query][value_feature] +=
                        probability*V[key][value_feature];
                }
            }
            for(int value_feature=0; value_feature<N; ++value_feature){
                expected_o[query][value_feature] /= expected_l[query];
            }
        }
    }

}  // namespace

int main(){
    fsa::dma_word_t q_memory[fsa::DMA_QKV_WORDS]{};
    fsa::dma_word_t k_memory[fsa::DMA_QKV_WORDS]{};
    fsa::dma_word_t vt_memory[fsa::DMA_QKV_WORDS]{};
    packElemMatrix(Q, q_memory);
    packElemMatrix(K, k_memory);
    packTransposedV(vt_memory);

    constexpr int CANARY_WORDS = 2;
    fsa::dma_word_t ol_memory[fsa::DMA_OL_WORDS+CANARY_WORDS]{};
    const fsa::dma_word_t canary =
        (fsa::dma_word_t)0x5a5aa5a55a5aa5a5ULL;
    for(int word=0; word<fsa::DMA_OL_WORDS+CANARY_WORDS; ++word){
        ol_memory[word] = canary;
    }

    ap_uint<8> status = 0xff;
    fsa_dma_top(
        q_memory,
        k_memory,
        vt_memory,
        ol_memory,
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
        ol_memory[fsa::DMA_OL_WORDS]==canary &&
            ol_memory[fsa::DMA_OL_WORDS+1]==canary,
        "OL DMA wrote beyond the fixed output region"
    );

    float expected_l[N]{};
    float expected_o[N][N]{};
    golden(expected_l, expected_o);

    for(int query=0; query<N; ++query){
        const float actual_l = readOlValue(ol_memory, query);
        expect(
            std::isfinite(actual_l) &&
                std::fabs(actual_l-expected_l[query])<=0.12F,
            "L mismatch at query "+std::to_string(query)
        );

        for(int value_feature=0; value_feature<N; ++value_feature){
            const int ol_index = N+query*N+value_feature;
            const float actual_o = readOlValue(ol_memory, ol_index);
            expect(
                std::isfinite(actual_o) &&
                    std::fabs(
                        actual_o-expected_o[query][value_feature]
                    )<=0.12F,
                "O mismatch at query "+std::to_string(query)+
                    ", feature "+std::to_string(value_feature)
            );
        }
    }

    if(failures!=0){
        std::cerr << "[FAIL] test_fsa_dma_top: "
                  << failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_fsa_dma_top: DDR Q/K/VT layout, FA and "
                 "query-major OL writeback are correct"
              << std::endl;
    return 0;
}
