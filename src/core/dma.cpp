#include "fsa/dma.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{

    elem_t dma_unpack_elem(const dma_word_t& word, const int lane){
        #pragma HLS INLINE
        const int low = lane*elemWidth;
        const ap_uint<elemWidth> bits = word.range(low+elemWidth-1, low);
        const fp_struct<elem_t> view(bits);
        return view.to_ieee();
    }

    dma_word_t dma_pack_elem_word(
        const elem_t values[DMA_ELEMS_PER_WORD]
    ){
        #pragma HLS INLINE
        dma_word_t word = 0;
        for(int lane=0; lane<DMA_ELEMS_PER_WORD; ++lane){
            #pragma HLS UNROLL
            const fp_struct<elem_t> view(values[lane]);
            const int low = lane*elemWidth;
            word.range(low+elemWidth-1, low) = view.data();
        }
        return word;
    }

    acc_t dma_unpack_acc(const dma_word_t& word, const int lane){
        #pragma HLS INLINE
        const int low = lane*accWidth;
        const ap_uint<accWidth> bits = word.range(low+accWidth-1, low);
        const fp_struct<acc_t> view(bits);
        return view.to_ieee();
    }

    dma_word_t dma_pack_acc_word(
        const acc_t values[DMA_ACCS_PER_WORD]
    ){
        #pragma HLS INLINE
        dma_word_t word = 0;
        for(int lane=0; lane<DMA_ACCS_PER_WORD; ++lane){
            #pragma HLS UNROLL
            const fp_struct<acc_t> view(values[lane]);
            const int low = lane*accWidth;
            word.range(low+accWidth-1, low) = view.data();
        }
        return word;
    }

    void dma_load_qkv(
        const dma_word_t q_memory[DMA_QKV_WORDS],
        const dma_word_t k_memory[DMA_QKV_WORDS],
        const dma_word_t vt_memory[DMA_QKV_WORDS],
        elem_t q[SA_COLS][SA_ROWS],
        elem_t k[SA_ROWS][SA_ROWS],
        elem_t v[SA_ROWS][SA_ROWS]
    ){
        #pragma HLS INLINE off

        for(int word_index=0; word_index<DMA_QKV_WORDS; ++word_index){
            #pragma HLS LOOP_TRIPCOUNT min=4 max=4
            const dma_word_t q_word = q_memory[word_index];
            const dma_word_t k_word = k_memory[word_index];
            const dma_word_t vt_word = vt_memory[word_index];

            for(int lane=0; lane<DMA_ELEMS_PER_WORD; ++lane){
                #pragma HLS UNROLL
                const int linear = word_index*DMA_ELEMS_PER_WORD+lane;

                const int q_query = linear/SA_ROWS;
                const int q_feature = linear%SA_ROWS;
                q[q_query][q_feature] = dma_unpack_elem(q_word, lane);

                const int k_key = linear/SA_ROWS;
                const int k_feature = linear%SA_ROWS;
                k[k_key][k_feature] = dma_unpack_elem(k_word, lane);

                const int value_feature = linear/SA_ROWS;
                const int key = linear%SA_ROWS;
                v[key][value_feature] = dma_unpack_elem(vt_word, lane);
            }
        }
    }

    void dma_store_ol(
        dma_word_t ol_memory[DMA_OL_WORDS],
        const acc_t l[SA_COLS],
        const acc_t o[SA_COLS][SA_ROWS]
    ){
        #pragma HLS INLINE off

        for(int word_index=0; word_index<DMA_OL_WORDS; ++word_index){
            #pragma HLS LOOP_TRIPCOUNT min=10 max=10
            acc_t values[DMA_ACCS_PER_WORD]{};
            #pragma HLS ARRAY_PARTITION variable=values type=complete dim=1

            for(int lane=0; lane<DMA_ACCS_PER_WORD; ++lane){
                #pragma HLS UNROLL
                const int linear = word_index*DMA_ACCS_PER_WORD+lane;
                if(linear<SA_COLS){
                    values[lane] = l[linear];
                }else{
                    const int o_linear = linear-SA_COLS;
                    const int query = o_linear/SA_ROWS;
                    const int value_feature = o_linear%SA_ROWS;
                    values[lane] = o[query][value_feature];
                }
            }
            ol_memory[word_index] = dma_pack_acc_word(values);
        }
    }

}  // namespace fsa
