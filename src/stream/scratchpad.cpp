#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    void scratchpadReadRow(
        const elem_t spad_sram[2][SPAD_ROWS][SA_ROWS],
        const unsigned bank,
        const int address,
        elem_t data[SA_ROWS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=1 max=1
        for(int feature=0; feature<SA_ROWS; ++feature){
            #pragma HLS UNROLL
            data[feature] = spad_sram[bank][address][feature];
        }
    }

    /**
     * DMA流先写入显式双缓冲Scratchpad，再通过唯一一拍整行读端口送入
     * InputDelayer。Q只从DDR读取一次/Query tile，但会为每个KV tile重播。
     */
    void scratchpadProcess(
        const unsigned length,
        ElemRowStream& q_dma_stream,
        ElemRowStream& k_dma_stream,
        ElemRowStream& v_dma_stream,
        CoreControlStream& control_in,
        CoreControlStream& control_out,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION \
            function instances=scratchpadReadRow limit=1

        // 与旧FSA Core相同，Q/K/V_t共享一个逻辑Scratchpad地址空间。
        // 最外层两个物理bank用于tile级ping-pong；最后一维是一整行，
        // 对应SA_ROWS个并行elem_t。
        elem_t spad_sram[2][SPAD_ROWS][SA_ROWS]{};
        bool q_valid[2][SA_COLS]{};
        bool k_valid[2][SA_COLS]{};
        #pragma HLS BIND_STORAGE variable=spad_sram type=ram_t2p impl=bram
        #pragma HLS ARRAY_PARTITION variable=spad_sram type=complete dim=1
        #pragma HLS ARRAY_RESHAPE variable=spad_sram type=complete dim=3
        #pragma HLS ARRAY_PARTITION variable=q_valid type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=k_valid type=complete dim=2

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned q_bank = query_tile&1U;

            for(int query_lane=0; query_lane<SA_COLS; ++query_lane){
                #pragma HLS PIPELINE II=1
                const ElemRowPacket packet = q_dma_stream.read();
                q_valid[q_bank][query_lane] = packet.valid;
                for(int feature=0; feature<SA_ROWS; ++feature){
                    #pragma HLS UNROLL
                    spad_sram[q_bank]
                        [SPAD_Q_BASE_ADDRESS+query_lane][feature] =
                        packet.data[feature];
                }
            }

            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const unsigned kv_bank = key_tile&1U;

                for(int key_lane=0; key_lane<SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    const ElemRowPacket k_packet = k_dma_stream.read();
                    const ElemRowPacket v_packet = v_dma_stream.read();
                    k_valid[kv_bank][key_lane] = k_packet.valid;
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        spad_sram[kv_bank]
                            [SPAD_K_BASE_ADDRESS+key_lane][feature] =
                            k_packet.data[feature];
                        // ATTENTION_VALUE按feature行读取V_t。
                        spad_sram[kv_bank]
                            [SPAD_VT_BASE_ADDRESS+feature][key_lane] =
                            v_packet.data[feature];
                    }
                }

                // 控制token与本tile的Scratchpad数据一起向下游推进。
                const CoreTileControl control = control_in.read();
                control_out.write(control);

                // FSA中P会覆盖PE reg，因此每个KV tile都从Q SRAM重载Q。
                for(int query_lane=0;
                        query_lane<SA_COLS; ++query_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket packet{};
                    packet.valid = q_valid[q_bank][query_lane];
                    elem_t row_data[SA_ROWS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=row_data complete dim=1
                    scratchpadReadRow(
                        spad_sram,
                        q_bank,
                        SPAD_Q_BASE_ADDRESS+query_lane,
                        row_data
                    );
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        packet.data[feature] = row_data[feature];
                    }
                    q_sa_stream.write(packet);
                }

                for(int key_lane=0; key_lane<SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket k_packet{};
                    k_packet.valid = k_valid[kv_bank][key_lane];
                    elem_t row_data[SA_ROWS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=row_data complete dim=1
                    scratchpadReadRow(
                        spad_sram,
                        kv_bank,
                        SPAD_K_BASE_ADDRESS+key_lane,
                        row_data
                    );
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        k_packet.data[feature] = row_data[feature];
                    }
                    k_sa_stream.write(k_packet);
                }

                // ATTENTION_VALUE按feature顺序整行读取V_t，再为当前
                // 多周期SA适配器恢复成每个key一个packet。
                elem_t v_transposed[SA_ROWS][SA_ROWS]{};
                #pragma HLS ARRAY_PARTITION \
                    variable=v_transposed complete dim=2
                for(int feature=0; feature<SA_ROWS; ++feature){
                    #pragma HLS PIPELINE II=1
                    scratchpadReadRow(
                        spad_sram,
                        kv_bank,
                        SPAD_VT_BASE_ADDRESS+feature,
                        v_transposed[feature]
                    );
                }
                for(int key_lane=0; key_lane<SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    ElemRowPacket v_packet{};
                    v_packet.valid = k_valid[kv_bank][key_lane];
                    for(int feature=0; feature<SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        v_packet.data[feature] =
                            v_transposed[feature][key_lane];
                    }
                    v_sa_stream.write(v_packet);
                }
            }
        }
    }

    /**
     * @brief 显式FSA InputDelayer阶段。
     *
     * Scratchpad以完整tile提供Q/K/V行。这里按旧ExecutionPlan的布局控制
     * 逐拍驱动唯一一套InputDelayer，并把错拍输出重新收集为下游SA使用的
     * tile。重新收集只适配当前多周期PE调度器；实际数据必须经过Delayer
     * 状态寄存器，不能再由SA直接索引原始K/V绕过该模块。
     */

}  // namespace streaming_v2_detail
}  // namespace fsa
