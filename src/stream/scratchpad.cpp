#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    using ScratchpadStorage = BankedSramStorage<
        elem_t, SPAD_ROWS, SA_ROWS, spadBanks, SPAD_SUB_BANKS
    >;

    void writeScratchpadBeat(
        ScratchpadStorage& storage,
        const SpadWritePacket& packet
    ){
        #pragma HLS INLINE
        elem_t values[ScratchpadStorage::SUB_BANK_SIZE];
        #pragma HLS ARRAY_PARTITION variable=values complete dim=1
        for(int lane=0; lane<ScratchpadStorage::SUB_BANK_SIZE; ++lane){
            #pragma HLS UNROLL
            values[lane] = dma_unpack_elem(packet.data, lane);
        }
        bankedSramNarrowWrite(
            storage, packet.address.to_uint(),
            packet.sub_bank.to_uint(), values
        );
    }

    ElemRowPacket readScratchpadRow(
        const ScratchpadStorage& storage,
        const unsigned address,
        const bool valid
    ){
        #pragma HLS INLINE
        ElemRowPacket packet{};
        packet.valid = valid;
        bankedSramFullRead<ScratchpadStorage, elem_t, SA_ROWS>(
            storage, address, packet.data
        );
        return packet;
    }

    /**
     * DMA使用64-bit narrow-write，SA侧使用唯一full-row read。逻辑地址
     * 与FSA-main一致，地址低位选择物理bank，Q/K/V各自使用双缓冲区。
     */
    void scratchpadProcess(
        const unsigned length,
        const bool causal,
        SpadWriteStream& q_dma_stream,
        SpadWriteStream& k_dma_stream,
        SpadWriteStream& v_dma_stream,
        CoreControlStream& control_in,
        CoreControlStream& control_out,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream
    ){
        #pragma HLS INLINE off

        ScratchpadStorage storage;
        bool q_valid[2][SA_COLS];
        bool k_valid[2][SA_COLS];
        #pragma HLS BIND_STORAGE variable=storage.data type=ram_t2p impl=bram
        #pragma HLS ARRAY_PARTITION variable=storage.data complete dim=1
        #pragma HLS ARRAY_PARTITION variable=storage.data complete dim=2
        #pragma HLS ARRAY_PARTITION variable=q_valid complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_valid complete dim=0

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned q_buffer = query_tile&1U;
            const unsigned q_base = q_buffer
                ? SPAD_Q1_BASE_ADDRESS : SPAD_Q0_BASE_ADDRESS;

            for(int lane=0; lane<SA_COLS; ++lane){
                for(int word=0; word<SPAD_SUB_BANKS; ++word){
                    #pragma HLS PIPELINE II=1
                    const SpadWritePacket packet = q_dma_stream.read();
                    writeScratchpadBeat(storage, packet);
                    if(word==0){
                        q_valid[q_buffer][lane] = packet.row_valid;
                    }
                }
            }

            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const unsigned kv_buffer = key_tile&1U;
                const unsigned k_base = kv_buffer
                    ? SPAD_K1_BASE_ADDRESS : SPAD_K0_BASE_ADDRESS;
                const unsigned v_base = kv_buffer
                    ? SPAD_V1_BASE_ADDRESS : SPAD_V0_BASE_ADDRESS;

                for(int lane=0; lane<SA_COLS; ++lane){
                    for(int word=0; word<SPAD_SUB_BANKS; ++word){
                        #pragma HLS PIPELINE II=1
                        const SpadWritePacket k_packet = k_dma_stream.read();
                        const SpadWritePacket v_packet = v_dma_stream.read();
                        writeScratchpadBeat(storage, k_packet);
                        writeScratchpadBeat(storage, v_packet);
                        if(word==0){
                            k_valid[kv_buffer][lane] = k_packet.row_valid;
                        }
                    }
                }

                const CoreTileControl control = control_in.read();
                control_out.write(control);

                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    q_sa_stream.write(readScratchpadRow(
                        storage, q_base+lane, q_valid[q_buffer][lane]
                    ));
                }
                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    k_sa_stream.write(readScratchpadRow(
                        storage, k_base+lane, k_valid[kv_buffer][lane]
                    ));
                }
                for(int lane=0; lane<SA_COLS; ++lane){
                    #pragma HLS PIPELINE II=1
                    v_sa_stream.write(readScratchpadRow(
                        storage, v_base+lane, k_valid[kv_buffer][lane]
                    ));
                }
            }
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
