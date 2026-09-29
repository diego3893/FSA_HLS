#include "fsa/stream/split_d/split_d_internal.hpp"

namespace fsa{
namespace split_d{
namespace detail{

    void loadElemTile(
        const dma_word_t memory[MAX_QKV_WORDS],
        const unsigned token_base,
        const unsigned active_tokens,
        elem_t tile[PE_DIM][HEAD_DIM]
    ){
        #pragma HLS INLINE
        #pragma HLS ARRAY_PARTITION variable=tile complete dim=1

        for(int token=0; token<PE_DIM; ++token){
            for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                #pragma HLS PIPELINE II=1
                const bool token_valid =
                    (unsigned)token<active_tokens;
                const dma_word_t packed = token_valid
                    ? memory[(token_base+(unsigned)token)*
                        (unsigned)QKV_WORDS_PER_TOKEN+(unsigned)word]
                    : (dma_word_t)0;
                for(int lane=0; lane<QKV_ELEMS_PER_WORD; ++lane){
                    #pragma HLS UNROLL
                    tile[token][word*QKV_ELEMS_PER_WORD+lane] =
                        token_valid ? dma_unpack_elem(packed, lane)
                                    : elemZero();
                }
            }
        }
    }

    void loadValueTile(
        const dma_word_t memory[MAX_QKV_WORDS],
        const unsigned token_base,
        const unsigned active_tokens,
        elem_t tile[PE_DIM][HEAD_DIM]
    ){
        #pragma HLS INLINE
        #pragma HLS ARRAY_PARTITION variable=tile complete dim=1

        for(int token=0; token<PE_DIM; ++token){
            for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                #pragma HLS PIPELINE II=1
                const bool token_valid =
                    (unsigned)token<active_tokens;
                const dma_word_t packed = token_valid
                    ? memory[(token_base+(unsigned)token)*
                        (unsigned)QKV_WORDS_PER_TOKEN+(unsigned)word]
                    : (dma_word_t)0;
                for(int lane=0; lane<QKV_ELEMS_PER_WORD; ++lane){
                    #pragma HLS UNROLL
                    tile[token][word*QKV_ELEMS_PER_WORD+lane] =
                        token_valid ? dma_unpack_elem(packed, lane)
                                    : elemZero();
                }
            }
        }
    }

    void storeOutputTile(
        dma_word_t memory[MAX_O_WORDS],
        const unsigned token_base,
        const unsigned active_tokens,
        const acc_t output[PE_DIM][HEAD_DIM]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=output complete dim=1

        const unsigned output_base =
            token_base*(unsigned)O_WORDS_PER_TOKEN;
        const unsigned total_words =
            active_tokens*(unsigned)O_WORDS_PER_TOKEN;
        for(unsigned index=0; index<total_words; ++index){
            #pragma HLS PIPELINE II=1
            const unsigned token =
                index/(unsigned)O_WORDS_PER_TOKEN;
            const unsigned word =
                index%(unsigned)O_WORDS_PER_TOKEN;
            acc_t values[O_ACCS_PER_WORD]{};
            #pragma HLS ARRAY_PARTITION variable=values complete dim=1
            for(int lane=0; lane<O_ACCS_PER_WORD; ++lane){
                #pragma HLS UNROLL
                values[lane] =
                    output[token][word*O_ACCS_PER_WORD+lane];
            }
            memory[output_base+index] = dma_pack_acc_word(values);
        }
    }

}  // namespace detail
}  // namespace split_d
}  // namespace fsa
