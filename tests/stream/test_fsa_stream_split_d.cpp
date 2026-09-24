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

    bool runCase(const unsigned length, const bool causal, const int seed){
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
        for(unsigned token=0; token<length; ++token){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                maximum_error = std::max(
                    maximum_error,
                    std::abs((double)actual[token][feature]-
                        expected[token][feature])
                );
            }
        }
        if(maximum_error > 0.03){
            std::cerr << "numerical failure: L=" << length
                << " causal=" << causal
                << " max_error=" << maximum_error << "\n";
            return false;
        }
        return true;
    }

}  // namespace

int main(){
    const unsigned primary_length = PE_DIM==4 ? 7U : 5U;
    if(!runCase(primary_length, false, 1604)){
        return 1;
    }
    if(!runCase(primary_length, true, 1605)){
        return 1;
    }
    if(!runCase(1U, false, 1606)){
        return 1;
    }
    if(PE_DIM==4 && HEAD_DIM==16){
        if(!runCase(16U, false, 1616)){
            return 1;
        }
        if(!runCase(16U, true, 1617)){
            return 1;
        }
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
        return 1;
    }

    std::cout << "[PASS] fsa_stream_split_d: PE=" << PE_DIM
        << "x" << PE_DIM << " HEAD_DIM=" << HEAD_DIM
        << " DIM_BLOCKS=" << DIM_BLOCKS << "\n";
    return 0;
}
