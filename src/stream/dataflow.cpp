#include "fsa/stream/common.hpp"

namespace fsa{
namespace streaming_v2_detail{

    /**
     * 规范DATAFLOW区域只包含局部stream声明和进程调用。外层参数检查不
     * 再妨碍Vitis把DMA、Core控制器、Scratchpad、InputDelayer、单一SA、
     * OutputDelayer、Accumulator和AccRAM抽取成并行进程。QK、softmax
     * 和PV仍在同一个SA进程中顺序复用唯一PE网格。
     */
    void fsaStreamingDataflow(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        const dma_word_t k_address[DMA_MAX_QKV_WORDS],
        const dma_word_t v_address[DMA_MAX_QKV_WORDS],
        dma_word_t o_address[DMA_MAX_O_WORDS],
        const unsigned length,
        const bool causal,
        ap_uint<8>& status
    ){
        #pragma HLS INLINE off

        ElemRowStream q_dma_stream("v2_q_dma");
        ElemRowStream k_dma_stream("v2_k_dma");
        ElemRowStream v_dma_stream("v2_v_dma");
        CoreControlStream control_to_spad("v2_control_to_spad");
        CoreControlStream control_to_delayer("v2_control_to_delayer");
        CoreControlStream control_to_sa("v2_control_to_sa");
        SaCycleControlStream sa_cycle_control_stream(
            "v2_sa_cycle_control"
        );
        ElemRowStream q_spad_stream("v2_q_spad");
        ElemRowStream k_spad_stream("v2_k_spad");
        ElemRowStream v_spad_stream("v2_v_spad");
        ElemRowStream q_sa_stream("v2_q_sa");
        ElemRowStream k_sa_stream("v2_k_sa");
        ElemRowStream v_sa_stream("v2_v_sa");
        SaResultStream raw_sa_result_stream("v2_raw_sa_result");
        SaResultStream aligned_sa_result_stream("v2_aligned_sa_result");
        AccRowStream output_stream("v2_output");
        DmaWordStream output_word_stream("v2_output_words");
        #pragma HLS STREAM variable=q_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_dma_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=control_to_spad depth=2
        #pragma HLS STREAM variable=control_to_delayer depth=2
        #pragma HLS STREAM variable=control_to_sa depth=2
        #pragma HLS STREAM variable=sa_cycle_control_stream depth=32
        #pragma HLS STREAM variable=q_spad_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_spad_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_spad_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=q_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=k_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=v_sa_stream depth=2*SA_COLS
        #pragma HLS STREAM variable=raw_sa_result_stream depth=2
        #pragma HLS STREAM variable=aligned_sa_result_stream depth=2
        #pragma HLS STREAM variable=output_stream depth=2
        #pragma HLS STREAM variable=output_word_stream \
            depth=2*SA_COLS*DMA_O_WORDS_PER_ROW
        #pragma HLS DATAFLOW

        fsaCoreControllerProcess(length, causal, control_to_spad);
        saExecutionPlanProcess(length, sa_cycle_control_stream);
        dmaReadQ(q_address, length, q_dma_stream);
        dmaReadK(k_address, length, k_dma_stream);
        dmaReadV(v_address, length, v_dma_stream);
        scratchpadProcess(
            length,
            q_dma_stream, k_dma_stream, v_dma_stream,
            control_to_spad, control_to_delayer,
            q_spad_stream, k_spad_stream, v_spad_stream
        );
        inputDelayerProcess(
            length,
            control_to_delayer,
            q_spad_stream, k_spad_stream, v_spad_stream,
            control_to_sa,
            q_sa_stream, k_sa_stream, v_sa_stream
        );
        systolicArrayProcess(
            length,
            control_to_sa, sa_cycle_control_stream,
            q_sa_stream, k_sa_stream, v_sa_stream,
            raw_sa_result_stream
        );
        outputDelayerProcess(
            length, raw_sa_result_stream, aligned_sa_result_stream
        );
        accumulatorProcess(
            length, aligned_sa_result_stream, output_stream
        );
        outputPackProcess(length, output_stream, output_word_stream);
        dmaWriteO(o_address, length, output_word_stream, status);
    }

}  // namespace streaming_v2_detail

void fsa_streaming_v2_run(
    const dma_word_t q_address[DMA_MAX_QKV_WORDS],
    const dma_word_t k_address[DMA_MAX_QKV_WORDS],
    const dma_word_t v_address[DMA_MAX_QKV_WORDS],
    dma_word_t o_address[DMA_MAX_O_WORDS],
    const ap_uint<32> sequence_length,
    const bool causal,
    ap_uint<8>& status
){
    status = (ap_uint<8>)static_cast<std::uint8_t>(
        FsaStreamingV2Status::INVALID_SEQUENCE_LENGTH
    );
    const unsigned length = sequence_length.to_uint();
    if(length==0 || length>(unsigned)MAX_SEQUENCE_LENGTH){
        return;
    }

    streaming_v2_detail::fsaStreamingDataflow(
        q_address, k_address, v_address, o_address,
        length, causal, status
    );
}

}  // namespace fsa
