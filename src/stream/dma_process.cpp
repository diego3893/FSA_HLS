#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    namespace{

        ap_uint<16> activeRows(
            const unsigned source_row,
            const unsigned length
        ){
            #pragma HLS INLINE
            const unsigned remaining = source_row<length
                ? length-source_row : 0U;
            return (ap_uint<16>)(remaining<(unsigned)SA_COLS
                ? remaining : (unsigned)SA_COLS);
        }

    }  // namespace

    /**
     * 统一产生DMA descriptor；Q/K/V仍由三个专用actor和三个AXI bundle
     * 并发执行。request_id和packet.last构成内部请求/完成协议。
     */
    void dmaRequestProcess(
        const unsigned length,
        const bool causal,
        DmaReadRequestStream& q_request_stream,
        DmaReadRequestStream& k_request_stream,
        DmaReadRequestStream& v_request_stream
    ){
        #pragma HLS INLINE off

        const unsigned tiles = tileCount(length);
        ap_uint<32> q_request_id = 0;
        ap_uint<32> kv_request_id = 0;
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            DmaReadRequest q_request{};
            q_request.kind = DmaTransferKind::Q;
            q_request.request_id = q_request_id++;
            q_request.source_row = query_tile*(unsigned)SA_COLS;
            q_request.scratchpad_base = (sram_address_t)(
                (query_tile&1U)
                    ? SPAD_Q1_BASE_ADDRESS : SPAD_Q0_BASE_ADDRESS
            );
            q_request.active_rows = activeRows(
                q_request.source_row.to_uint(), length
            );
            q_request_stream.write(q_request);

            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS PIPELINE II=1
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                DmaReadRequest k_request{};
                DmaReadRequest v_request{};
                k_request.kind = DmaTransferKind::K;
                v_request.kind = DmaTransferKind::V;
                k_request.request_id = kv_request_id;
                v_request.request_id = kv_request_id++;
                const unsigned source_row =
                    key_tile*(unsigned)SA_COLS;
                k_request.source_row = source_row;
                v_request.source_row = source_row;
                k_request.scratchpad_base = (sram_address_t)(
                    (key_tile&1U)
                        ? SPAD_K1_BASE_ADDRESS : SPAD_K0_BASE_ADDRESS
                );
                v_request.scratchpad_base = (sram_address_t)(
                    (key_tile&1U)
                        ? SPAD_V1_BASE_ADDRESS : SPAD_V0_BASE_ADDRESS
                );
                const ap_uint<16> rows = activeRows(source_row, length);
                k_request.active_rows = rows;
                v_request.active_rows = rows;
                k_request_stream.write(k_request);
                v_request_stream.write(v_request);
            }
        }
    }

    void dmaReadQ(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        const unsigned length,
        DmaReadRequestStream& request_stream,
        SpadWriteStream& q_dma_stream
    ){
        #pragma HLS INLINE off

        const unsigned query_tiles = tileCount(length);
        for(unsigned query_tile=0;
                query_tile<query_tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const DmaReadRequest request = request_stream.read();
            for(int lane=0; lane<SA_COLS; ++lane){
                const unsigned query =
                    request.source_row.to_uint()+(unsigned)lane;
                for(int word=0; word<SPAD_SUB_BANKS; ++word){
                    #pragma HLS PIPELINE II=1
                    SpadWritePacket packet{};
                    packet.kind = request.kind;
                    packet.request_id = request.request_id;
                    packet.address = (sram_address_t)(
                        request.scratchpad_base.to_uint()+lane
                    );
                    packet.sub_bank =
                        (sub_bank_index_t<SPAD_SUB_BANKS>)word;
                    packet.row_valid =
                        lane<request.active_rows.to_int();
                    packet.transfer_last = lane+1==SA_COLS &&
                        word+1==SPAD_SUB_BANKS;
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
        DmaReadRequestStream& request_stream,
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
                const DmaReadRequest request = request_stream.read();
                for(int lane=0; lane<SA_COLS; ++lane){
                    const unsigned key =
                        request.source_row.to_uint()+(unsigned)lane;
                    for(int word=0; word<SPAD_SUB_BANKS; ++word){
                        #pragma HLS PIPELINE II=1
                        SpadWritePacket packet{};
                        packet.kind = request.kind;
                        packet.request_id = request.request_id;
                        packet.address = (sram_address_t)(
                            request.scratchpad_base.to_uint()+lane
                        );
                        packet.sub_bank =
                            (sub_bank_index_t<SPAD_SUB_BANKS>)word;
                        packet.row_valid =
                            lane<request.active_rows.to_int();
                        packet.transfer_last = lane+1==SA_COLS &&
                            word+1==SPAD_SUB_BANKS;
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
        DmaReadRequestStream& request_stream,
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
                const DmaReadRequest request = request_stream.read();
                for(int lane=0; lane<SA_COLS; ++lane){
                    const unsigned key =
                        request.source_row.to_uint()+(unsigned)lane;
                    for(int word=0; word<SPAD_SUB_BANKS; ++word){
                        #pragma HLS PIPELINE II=1
                        SpadWritePacket packet{};
                        packet.kind = request.kind;
                        packet.request_id = request.request_id;
                        packet.address = (sram_address_t)(
                            request.scratchpad_base.to_uint()+lane
                        );
                        packet.sub_bank =
                            (sub_bank_index_t<SPAD_SUB_BANKS>)word;
                        packet.row_valid =
                            lane<request.active_rows.to_int();
                        packet.transfer_last = lane+1==SA_COLS &&
                            word+1==SPAD_SUB_BANKS;
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
