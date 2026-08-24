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

    void dma_load_elem_row(
        const dma_word_t memory[DMA_MAX_QKV_WORDS],
        const unsigned row_index,
        elem_t row[SA_ROWS]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=row type=complete dim=1

        const unsigned base = row_index*DMA_QKV_WORDS_PER_ROW;
        for(int word_index=0;
                word_index<DMA_QKV_WORDS_PER_ROW; ++word_index){
            #pragma HLS PIPELINE II=1
            const dma_word_t word = memory[base+word_index];
            for(int lane=0; lane<DMA_ELEMS_PER_WORD; ++lane){
                #pragma HLS UNROLL
                row[word_index*DMA_ELEMS_PER_WORD+lane] =
                    dma_unpack_elem(word, lane);
            }
        }
    }

    void dma_store_acc_row(
        dma_word_t memory[DMA_MAX_O_WORDS],
        const unsigned row_index,
        const acc_t row[SA_ROWS]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=row type=complete dim=1

        const unsigned base = row_index*DMA_O_WORDS_PER_ROW;
        for(int word_index=0;
                word_index<DMA_O_WORDS_PER_ROW; ++word_index){
            #pragma HLS PIPELINE II=1
            acc_t values[DMA_ACCS_PER_WORD]{};
            #pragma HLS ARRAY_PARTITION variable=values type=complete dim=1
            for(int lane=0; lane<DMA_ACCS_PER_WORD; ++lane){
                #pragma HLS UNROLL
                values[lane] = row[word_index*DMA_ACCS_PER_WORD+lane];
            }
            memory[base+word_index] = dma_pack_acc_word(values);
        }
    }

}  // namespace fsa
