#include "fsa/stream/split_d/fsa_stream_split_d.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace{

    using fsa::acc_t;
    using fsa::dma_word_t;
    using fsa::elem_t;
    using namespace fsa::split_d;

    static dma_word_t q_memory[MAX_QKV_WORDS];
    static dma_word_t k_memory[MAX_QKV_WORDS];
    static dma_word_t v_memory[MAX_QKV_WORDS];
    static dma_word_t o_memory[MAX_O_WORDS];

    enum class InputPattern{
        Random,
        OnesV,
        BasisV,
        BasisVLastFeature
    };

    void packInput(
        const std::vector<std::vector<elem_t>>& input,
        dma_word_t memory[MAX_QKV_WORDS]
    ){
        for(std::size_t token=0; token<input.size(); ++token){
            for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                elem_t values[QKV_ELEMS_PER_WORD]{};
                for(int lane=0; lane<QKV_ELEMS_PER_WORD; ++lane){
                    values[lane] =
                        input[token][word*QKV_ELEMS_PER_WORD+lane];
                }
                memory[token*(std::size_t)QKV_WORDS_PER_TOKEN+
                    (std::size_t)word] = fsa::dma_pack_elem_word(values);
            }
        }
    }

    std::vector<std::vector<float>> unpackOutput(const unsigned length){
        std::vector<std::vector<float>> output(
            length, std::vector<float>(HEAD_DIM, 0.0F)
        );
        for(unsigned token=0; token<length; ++token){
            for(int word=0; word<O_WORDS_PER_TOKEN; ++word){
                const dma_word_t packed =
                    o_memory[token*(unsigned)O_WORDS_PER_TOKEN+
                        (unsigned)word];
                for(int lane=0; lane<O_ACCS_PER_WORD; ++lane){
                    output[token][word*O_ACCS_PER_WORD+lane] =
                        fsa::dma_unpack_acc(packed, lane);
                }
            }
        }
        return output;
    }

    std::vector<std::vector<double>> reference(
        const std::vector<std::vector<elem_t>>& q,
        const std::vector<std::vector<elem_t>>& k,
        const std::vector<std::vector<elem_t>>& v,
        const bool causal
    ){
        const unsigned length = (unsigned)q.size();
        std::vector<std::vector<double>> output(
            length, std::vector<double>(HEAD_DIM, 0.0)
        );
        for(unsigned query=0; query<length; ++query){
            const unsigned key_count = causal ? query+1U : length;
            std::vector<double> score(key_count, 0.0);
            for(unsigned key=0; key<key_count; ++key){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    score[key] += (double)q[query][feature]*
                        (double)k[key][feature];
                }
            }
            const double maximum =
                *std::max_element(score.begin(), score.end());
            std::vector<double> probability(key_count, 0.0);
            double denominator = 0.0;
            for(unsigned key=0; key<key_count; ++key){
                probability[key] = std::exp(
                    (score[key]-maximum)/std::sqrt((double)HEAD_DIM)
                );
                denominator += probability[key];
            }
            for(int feature=0; feature<HEAD_DIM; ++feature){
                for(unsigned key=0; key<key_count; ++key){
                    output[query][feature] += probability[key]*
                        (double)v[key][feature];
                }
                output[query][feature] /= denominator;
            }
        }
        return output;
    }

    bool runCase(
        const char* name,
        const unsigned length,
        const bool causal,
        const int seed,
        const InputPattern pattern
    ){
        std::mt19937 generator(seed);
        std::uniform_real_distribution<float> qk_distribution(-0.5F, 0.5F);
        std::uniform_real_distribution<float> v_distribution(-1.0F, 1.0F);
        std::vector<std::vector<elem_t>> q(
            length, std::vector<elem_t>(HEAD_DIM)
        );
        std::vector<std::vector<elem_t>> k = q;
        std::vector<std::vector<elem_t>> v = q;
        for(unsigned token=0; token<length; ++token){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                q[token][feature] = (elem_t)qk_distribution(generator);
                k[token][feature] = (elem_t)qk_distribution(generator);
                v[token][feature] = (elem_t)v_distribution(generator);
            }
        }
        if(pattern == InputPattern::OnesV){
            for(unsigned token=0; token<length; ++token){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    v[token][feature] = (elem_t)1.0F;
                }
            }
        }else if(pattern==InputPattern::BasisV ||
                pattern==InputPattern::BasisVLastFeature){
            const int probe_feature =
                pattern==InputPattern::BasisV ? 0 : HEAD_DIM-1;
            for(unsigned token=0; token<length; ++token){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    q[token][feature] = (elem_t)0.0F;
                    k[token][feature] = (elem_t)0.0F;
                    v[token][feature] = (elem_t)0.0F;
                }
                q[token][probe_feature] = (elem_t)1.0F;
            }
            k[0][probe_feature] = (elem_t)0.5F;
            k[1][probe_feature] = (elem_t)-0.5F;
            v[0][0] = (elem_t)1.0F;
            v[1][1] = (elem_t)1.0F;
        }

        std::fill(q_memory, q_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(k_memory, k_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(v_memory, v_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(o_memory, o_memory+MAX_O_WORDS, (dma_word_t)0);
        packInput(q, q_memory);
        packInput(k, k_memory);
        packInput(v, v_memory);

        ap_uint<8> status = 0xff;
        fsa_stream_split_d(
            q_memory, k_memory, v_memory, o_memory,
            (ap_uint<32>)length, causal, status
        );
        if(status != 0){
            std::cerr << "status failure: " << status.to_uint() << "\n";
            return false;
        }

        const std::vector<std::vector<float>> actual =
            unpackOutput(length);
        const std::vector<std::vector<double>> expected =
            reference(q, k, v, causal);
        double maximum_error = 0.0;
        unsigned maximum_token = 0;
        int maximum_feature = 0;
        unsigned first_token = 0;
        int first_feature = 0;
        bool first_error_found = false;
        for(unsigned token=0; token<length; ++token){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                const double error = std::abs(
                    (double)actual[token][feature]-
                    expected[token][feature]
                );
                if(error > maximum_error){
                    maximum_error = error;
                    maximum_token = token;
                    maximum_feature = feature;
                }
                if(!first_error_found && error>0.03){
                    first_token = token;
                    first_feature = feature;
                    first_error_found = true;
                }
            }
        }
        if(maximum_error > 0.03){
            std::cerr << "numerical failure: case=" << name
                << " L=" << length
                << " causal=" << causal
                << " max_error=" << maximum_error
                << " max_at=[" << maximum_token << "]["
                << maximum_feature << "]"
                << " actual=" << actual[maximum_token][maximum_feature]
                << " expected=" << expected[maximum_token][maximum_feature]
                << " first_at=[" << first_token << "]["
                << first_feature << "]"
                << " first_actual=" << actual[first_token][first_feature]
                << " first_expected=" << expected[first_token][first_feature]
                << "\n";
            if((pattern==InputPattern::BasisV ||
                    pattern==InputPattern::BasisVLastFeature) && length>=2U){
                std::cerr << "basis output: q0=[" << actual[0][0]
                    << ", " << actual[0][1] << "] q1=["
                    << actual[1][0] << ", " << actual[1][1]
                    << "] expected=[" << expected[0][0]
                    << ", " << expected[0][1] << "]\n";
            }
            return false;
        }
        return true;
    }

}  // namespace

int main(){
    const unsigned primary_length = PE_DIM==4 ? 7U : 5U;
    bool passed = true;
    passed = runCase(
        "single-key-first", 1U, false, 1606, InputPattern::Random
    ) && passed;
    passed = runCase(
        "two-key-ones-v", 2U, false, 1607, InputPattern::OnesV
    ) && passed;
    passed = runCase(
        "two-key-basis-v", 2U, false, 1608, InputPattern::BasisV
    ) && passed;
    passed = runCase(
        "two-key-basis-v-last-feature", 2U, false, 1609,
        InputPattern::BasisVLastFeature
    ) && passed;
    passed = runCase(
        "primary-noncausal", primary_length, false, 1604,
        InputPattern::Random
    ) && passed;
    passed = runCase(
        "primary-causal", primary_length, true, 1605,
        InputPattern::Random
    ) && passed;
    if(PE_DIM==4 && HEAD_DIM==16){
        passed = runCase(
            "full-tile-noncausal", 16U, false, 1616,
            InputPattern::Random
        ) && passed;
        passed = runCase(
            "full-tile-causal", 16U, true, 1617,
            InputPattern::Random
        ) && passed;
    }

    std::fill(o_memory, o_memory+MAX_O_WORDS, (dma_word_t)0);
    o_memory[0] = (dma_word_t)0x12345678U;
    ap_uint<8> status = 0;
    fsa_stream_split_d(
        q_memory, k_memory, v_memory, o_memory,
        (ap_uint<32>)0, false, status
    );
    if(status!=1 || o_memory[0]!=(dma_word_t)0x12345678U){
        std::cerr << "invalid-length canary failure\n";
        passed = false;
    }
    if(!passed){
        return 1;
    }

    std::cout << "[PASS] fsa_stream_split_d: PE=" << PE_DIM
        << "x" << PE_DIM << " HEAD_DIM=" << HEAD_DIM
        << " DIM_BLOCKS=" << DIM_BLOCKS << "\n";
    return 0;
}
