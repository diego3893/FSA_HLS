#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    void dmaReadQ(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        const unsigned length,
        SpadWriteStream& q_dma_stream
    ){
        #pragma HLS INLINE off

        const unsigned query_tiles = tileCount(length);
        for(unsigned query_tile=0;
                query_tile<query_tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned base = (query_tile&1U)
                ? SPAD_Q1_BASE_ADDRESS : SPAD_Q0_BASE_ADDRESS;
            for(int lane=0; lane<SA_COLS; ++lane){
                const unsigned query =
                    query_tile*(unsigned)SA_COLS+(unsigned)lane;
                for(int word=0; word<SPAD_SUB_BANKS; ++word){
                    #pragma HLS PIPELINE II=1
                    SpadWritePacket packet{};
                    packet.address = (sram_address_t)(base+lane);
                    packet.sub_bank =
                        (sub_bank_index_t<SPAD_SUB_BANKS>)word;
                    packet.row_valid = query<length;
                    packet.data = packet.row_valid
                        ? q_address[query*DMA_QKV_WORDS_PER_ROW+word]
                        : (dma_word_t)0;
                    q_dma_stream.write(packet);
                }
            }
        }
    }

    void dmaReadK(
        const dma_word_t k_address[DMA_MAX_QKV_WORDS],
        const unsigned length,
        const bool causal,
        SpadWriteStream& k_dma_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const unsigned base = (key_tile&1U)
                    ? SPAD_K1_BASE_ADDRESS : SPAD_K0_BASE_ADDRESS;
                for(int lane=0; lane<SA_COLS; ++lane){
                    const unsigned key =
                        key_tile*(unsigned)SA_COLS+(unsigned)lane;
                    for(int word=0; word<SPAD_SUB_BANKS; ++word){
                        #pragma HLS PIPELINE II=1
                        SpadWritePacket packet{};
                        packet.address = (sram_address_t)(base+lane);
                        packet.sub_bank =
                            (sub_bank_index_t<SPAD_SUB_BANKS>)word;
                        packet.row_valid = key<length;
                        packet.data = packet.row_valid
                            ? k_address[key*DMA_QKV_WORDS_PER_ROW+word]
                            : (dma_word_t)0;
                        k_dma_stream.write(packet);
                    }
                }
            }
        }
    }

    void dmaReadV(
        const dma_word_t v_address[DMA_MAX_QKV_WORDS],
        const unsigned length,
        const bool causal,
        SpadWriteStream& v_dma_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const unsigned base = (key_tile&1U)
                    ? SPAD_V1_BASE_ADDRESS : SPAD_V0_BASE_ADDRESS;
                for(int lane=0; lane<SA_COLS; ++lane){
                    const unsigned key =
                        key_tile*(unsigned)SA_COLS+(unsigned)lane;
                    for(int word=0; word<SPAD_SUB_BANKS; ++word){
                        #pragma HLS PIPELINE II=1
                        SpadWritePacket packet{};
                        packet.address = (sram_address_t)(base+lane);
                        packet.sub_bank =
                            (sub_bank_index_t<SPAD_SUB_BANKS>)word;
                        packet.row_valid = key<length;
                        packet.data = packet.row_valid
                            ? v_address[key*DMA_QKV_WORDS_PER_ROW+word]
                            : (dma_word_t)0;
                        v_dma_stream.write(packet);
                    }
                }
            }
        }
    }

    /**
     * 单一扁平循环产生完整O矩阵的连续地址写，便于m_axi合并成长burst。
     */
    void dmaWriteO(
        dma_word_t o_address[DMA_MAX_O_WORDS],
        const unsigned length,
        DmaWordStream& output_word_stream,
        ap_uint<8>& status
    ){
        #pragma HLS INLINE off

        const unsigned total_words =
            length*(unsigned)DMA_O_WORDS_PER_ROW;
        for(unsigned word=0; word<total_words; ++word){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT \
                min=DMA_O_WORDS_PER_ROW max=DMA_MAX_O_WORDS
            o_address[word] = output_word_stream.read();
        }
        status = (ap_uint<8>)static_cast<std::uint8_t>(
            FsaStreamingV2Status::OK
        );
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
