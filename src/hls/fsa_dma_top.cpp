#include "fsa/hls/fsa_dma_top.hpp"

#include <hls_stream.h>

#include "fsa/hls/fsa_core_request_top.hpp"
#include "fsa/stream_array.hpp"

namespace{

    struct DmaTileMeta{
        bool reset = false;
        bool initialize = false;
        bool finalize = false;
        bool causal = false;
        std::uint16_t active_keys = 0;
        unsigned query_base = 0;
        unsigned key_base = 0;
    };

    struct DmaElemRow{
        fsa::elem_t data[fsa::SA_ROWS]{};
    };

    struct DmaAccRow{
        fsa::acc_t data[fsa::SA_ROWS]{};
    };

    void dmaLoadProcess(
        const fsa::dma_word_t q[fsa::DMA_MAX_QKV_WORDS],
        const fsa::dma_word_t k[fsa::DMA_MAX_QKV_WORDS],
        const fsa::dma_word_t v[fsa::DMA_MAX_QKV_WORDS],
        const unsigned length,
        const bool causal,
        hls::stream<DmaTileMeta>& meta_stream,
        hls::stream<DmaElemRow>& q_stream,
        hls::stream<DmaElemRow>& k_stream,
        hls::stream<DmaElemRow>& v_stream
    ){
        #pragma HLS INLINE off

        for(unsigned query_base=0;
                query_base<length; query_base+=fsa::SA_COLS){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=4096
            for(unsigned key_base=0;
                    key_base<length; key_base+=fsa::SA_COLS){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=4096

                DmaTileMeta meta{};
                meta.reset = query_base==0 && key_base==0;
                meta.initialize = key_base==0;
                meta.finalize = key_base+fsa::SA_COLS>=length;
                meta.causal = causal;
                meta.query_base = query_base;
                meta.key_base = key_base;
                const unsigned remaining_keys = length-key_base;
                meta.active_keys = remaining_keys<(unsigned)fsa::SA_COLS
                    ? (std::uint16_t)remaining_keys
                    : (std::uint16_t)fsa::SA_COLS;
                meta_stream.write(meta);

                for(int query_lane=0;
                        query_lane<fsa::SA_COLS; ++query_lane){
                    DmaElemRow row{};
                    const unsigned query =
                        query_base+(unsigned)query_lane;
                    if(query<length){
                        fsa::dma_load_elem_row(q, query, row.data);
                    }
                    q_stream.write(row);
                }

                for(int key_lane=0;
                        key_lane<fsa::SA_COLS; ++key_lane){
                    DmaElemRow k_row{};
                    DmaElemRow v_row{};
                    const unsigned key = key_base+(unsigned)key_lane;
                    if(key<length){
                        fsa::dma_load_elem_row(k, key, k_row.data);
                        fsa::dma_load_elem_row(v, key, v_row.data);
                    }
                    k_stream.write(k_row);
                    v_stream.write(v_row);
                }
            }
        }
    }

    void dmaComputeProcess(
        const unsigned length,
        hls::stream<DmaTileMeta>& meta_stream,
        hls::stream<DmaElemRow>& q_stream,
        hls::stream<DmaElemRow>& k_stream,
        hls::stream<DmaElemRow>& v_stream,
        hls::stream<DmaAccRow>& output_stream,
        hls::stream<ap_uint<8>>& status_stream
    ){
        #pragma HLS INLINE off
        bool protocol_error = false;

        for(unsigned query_base=0;
                query_base<length; query_base+=fsa::SA_COLS){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=4096
            for(unsigned key_base=0;
                    key_base<length; key_base+=fsa::SA_COLS){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=4096

                const DmaTileMeta meta = meta_stream.read();
                fsa::FsaCoreRequestInput request{};
                fsa::FsaCoreRequestOutput response{};
                #pragma HLS ARRAY_PARTITION variable=request.q type=complete dim=0
                #pragma HLS ARRAY_PARTITION variable=request.k type=complete dim=2
                #pragma HLS ARRAY_PARTITION variable=request.v type=complete dim=2
                #pragma HLS ARRAY_PARTITION variable=response.o type=complete dim=0

                request.reset = meta.reset;
                request.request_valid = true;
                request.initialize = meta.initialize;
                request.finalize = meta.finalize;
                request.causal = meta.causal;
                request.active_keys = meta.active_keys;
                request.query_base = meta.query_base;
                request.key_base = meta.key_base;

                for(int query_lane=0;
                        query_lane<fsa::SA_COLS; ++query_lane){
                    #pragma HLS PIPELINE II=1
                    const DmaElemRow row = q_stream.read();
                    for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        request.q[query_lane][feature] = row.data[feature];
                    }
                }
                for(int key_lane=0;
                        key_lane<fsa::SA_COLS; ++key_lane){
                    #pragma HLS PIPELINE II=1
                    const DmaElemRow k_row = k_stream.read();
                    const DmaElemRow v_row = v_stream.read();
                    for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                        #pragma HLS UNROLL
                        request.k[key_lane][feature] = k_row.data[feature];
                        request.v[key_lane][feature] = v_row.data[feature];
                    }
                }

                fsa::fsa_stream_request_run(request, response);
                protocol_error = protocol_error ||
                    response.protocol_error || !response.request_done;

                if(meta.finalize){
                    protocol_error = protocol_error || !response.normalized;
                    for(int query_lane=0;
                            query_lane<fsa::SA_COLS; ++query_lane){
                        const unsigned query =
                            query_base+(unsigned)query_lane;
                        if(query<length){
                            DmaAccRow row{};
                            for(int feature=0;
                                    feature<fsa::SA_ROWS; ++feature){
                                #pragma HLS UNROLL
                                row.data[feature] =
                                    response.o[query_lane][feature];
                            }
                            output_stream.write(row);
                        }
                    }
                }
            }
        }

        const fsa::FsaDmaStatus status = protocol_error
            ? fsa::FsaDmaStatus::CORE_PROTOCOL_ERROR
            : fsa::FsaDmaStatus::OK;
        status_stream.write((ap_uint<8>)static_cast<std::uint8_t>(status));
    }

    void dmaStoreProcess(
        fsa::dma_word_t o[fsa::DMA_MAX_O_WORDS],
        const unsigned length,
        hls::stream<DmaAccRow>& output_stream,
        hls::stream<ap_uint<8>>& status_stream,
        ap_uint<8>& status
    ){
        #pragma HLS INLINE off
        for(unsigned query=0; query<length; ++query){
            const DmaAccRow row = output_stream.read();
            fsa::dma_store_acc_row(o, query, row.data);
        }
        status = status_stream.read();
    }

}  // namespace

void fsa_dma_top(
    const fsa::dma_word_t q[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t k[fsa::DMA_MAX_QKV_WORDS],
    const fsa::dma_word_t v[fsa::DMA_MAX_QKV_WORDS],
    fsa::dma_word_t o[fsa::DMA_MAX_O_WORDS],
    const ap_uint<32> sequence_length,
    const bool causal,
    ap_uint<8>& status
){
    #pragma HLS ALLOCATION function instances=fsa::dma_load_elem_row limit=1
    #pragma HLS ALLOCATION function instances=fsa::dma_store_acc_row limit=1

    #pragma HLS INTERFACE m_axi port=q offset=slave bundle=gmem depth=FSA_DMA_AXI_QKV_DEPTH
    #pragma HLS INTERFACE m_axi port=k offset=slave bundle=gmem depth=FSA_DMA_AXI_QKV_DEPTH
    #pragma HLS INTERFACE m_axi port=v offset=slave bundle=gmem depth=FSA_DMA_AXI_QKV_DEPTH
    #pragma HLS INTERFACE m_axi port=o offset=slave bundle=gmem depth=FSA_DMA_AXI_O_DEPTH

    #pragma HLS INTERFACE s_axilite port=q bundle=control
    #pragma HLS INTERFACE s_axilite port=k bundle=control
    #pragma HLS INTERFACE s_axilite port=v bundle=control
    #pragma HLS INTERFACE s_axilite port=o bundle=control
    #pragma HLS INTERFACE s_axilite port=sequence_length bundle=control
    #pragma HLS INTERFACE s_axilite port=causal bundle=control
    #pragma HLS INTERFACE s_axilite port=status bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    status = (ap_uint<8>)static_cast<std::uint8_t>(
        fsa::FsaDmaStatus::INVALID_SEQUENCE_LENGTH
    );
    const unsigned length = sequence_length.to_uint();
    if(length==0 || length>(unsigned)fsa::MAX_SEQUENCE_LENGTH){
        return;
    }
    status = (ap_uint<8>)static_cast<std::uint8_t>(
        fsa::FsaDmaStatus::CORE_PROTOCOL_ERROR
    );

    hls::stream<DmaTileMeta> meta_stream("dma_meta");
    hls::stream<DmaElemRow> q_stream("dma_q_rows");
    hls::stream<DmaElemRow> k_stream("dma_k_rows");
    hls::stream<DmaElemRow> v_stream("dma_v_rows");
    hls::stream<DmaAccRow> output_stream("dma_o_rows");
    hls::stream<ap_uint<8>> status_stream("dma_status");
    #pragma HLS STREAM variable=meta_stream depth=2
    #pragma HLS STREAM variable=q_stream depth=2
    #pragma HLS STREAM variable=k_stream depth=2
    #pragma HLS STREAM variable=v_stream depth=2
    #pragma HLS STREAM variable=output_stream depth=2
    #pragma HLS STREAM variable=status_stream depth=1
    #pragma HLS DATAFLOW

    dmaLoadProcess(
        q, k, v, length, causal,
        meta_stream, q_stream, k_stream, v_stream
    );
    dmaComputeProcess(
        length,
        meta_stream, q_stream, k_stream, v_stream,
        output_stream, status_stream
    );
    dmaStoreProcess(o, length, output_stream, status_stream, status);
}
