/**
 * @file common.hpp
 * @brief streaming v2各DATAFLOW部件之间共享的内部协议。
 */
#ifndef FSA_STREAM_COMMON_HPP
#define FSA_STREAM_COMMON_HPP

#include <cstdint>

#include <hls_stream.h>

#include "fsa/stream/state.hpp"
#include "fsa/stream/accumulator.hpp"
#include "fsa/stream/arithmetic.hpp"
#include "fsa/stream/banked_sram.hpp"
#include "fsa/stream/delayer.hpp"
#include "fsa/stream/fsa_streaming_v2.hpp"

namespace fsa{
namespace streaming_v2_detail{

    static_assert(
        SA_ROWS>=SA_COLS,
        "streaming v2要求SA高度不小于token tile宽度"
    );

    // 与FSA-main相同的双缓冲逻辑地址布局。bank是地址低位选择的物理
    // SRAM bank，不再额外作为一维数组复制整个逻辑地址空间。
    constexpr int SPAD_Q0_BASE_ADDRESS = 0;
    constexpr int SPAD_Q1_BASE_ADDRESS = SPAD_Q0_BASE_ADDRESS+SA_COLS;
    constexpr int SPAD_K0_BASE_ADDRESS = SPAD_Q1_BASE_ADDRESS+SA_COLS;
    constexpr int SPAD_K1_BASE_ADDRESS = SPAD_K0_BASE_ADDRESS+SA_ROWS;
    constexpr int SPAD_V0_BASE_ADDRESS = SPAD_K1_BASE_ADDRESS+SA_ROWS;
    constexpr int SPAD_V1_BASE_ADDRESS = SPAD_V0_BASE_ADDRESS+SA_ROWS;

    static_assert(
        SPAD_V1_BASE_ADDRESS+SA_ROWS==SPAD_ROWS,
        "streaming v2的Q/K/V双缓冲布局必须覆盖整个Scratchpad"
    );

    struct ElemRowPacket{
        bool valid = false;
        elem_t data[SA_ROWS]{};
    };

    enum class DmaTransferKind : std::uint8_t{
        Q = 0,
        K = 1,
        V = 2
    };

    /** 单输出请求actor发给专用物理DMA actor的统一内部读请求。 */
    struct DmaReadRequest{
        DmaTransferKind kind = DmaTransferKind::Q;
        ap_uint<32> request_id = 0;
        ap_uint<32> source_row = 0;
        sram_address_t scratchpad_base = 0;
        ap_uint<16> active_rows = 0;
    };

    /** 一个AXI beat对应一次Scratchpad narrow-write。 */
    struct SpadWritePacket{
        DmaTransferKind kind = DmaTransferKind::Q;
        ap_uint<32> request_id = 0;
        sram_address_t address = 0;
        sub_bank_index_t<SPAD_SUB_BANKS> sub_bank = 0;
        bool row_valid = false;
        // last是该request的显式完成标记；Scratchpad不再依赖DMA actor
        // 的隐含循环边界判断tile是否可读。
        bool transfer_last = false;
        dma_word_t data = 0;
    };

    enum class DelayerPhase : std::uint8_t{
        LOAD_Q = 0,
        SCORE_K = 1,
        VALUE_V = 2
    };

    struct InputLayoutControl{
        bool rev_input = false;
        bool delay_output = false;
        bool rev_output = false;
    };

    /** InputDelayer每推进一拍产生一个beat，bubble也必须显式发送。 */
    struct DelayedElemBeat{
        DelayerPhase phase = DelayerPhase::LOAD_Q;
        ap_uint<16> cycle = 0;
        InputLayoutControl layout{};
        elem_t data[SA_ROWS]{};
    };

    struct TileMeta{
        bool initialize = false;
        bool finalize = false;
        bool causal = false;
        ap_uint<16> active_queries = 0;
        ap_uint<16> active_keys = 0;
        ap_uint<32> query_base = 0;
        ap_uint<32> key_base = 0;
    };

    struct CoreTileControl{
        TileMeta meta{};
        InputLayoutControl load_stationary{};
        InputLayoutControl attention_score{};
        InputLayoutControl attention_value{};
    };

    enum class SaResultKind : std::uint8_t{
        MAX_DIFF = 0,
        ROW_SUM = 1,
        PV = 2
    };

    struct SaResultToken{
        SaResultKind kind = SaResultKind::MAX_DIFF;
        bool initialize = false;
        bool finalize = false;
        ap_uint<16> active_queries = 0;
        ap_uint<16> index = 0;
        acc_t data[SA_COLS]{};
    };

    using ElemRowStream = hls::stream<ElemRowPacket>;
    using DmaReadRequestStream = hls::stream<DmaReadRequest>;
    using SpadWriteStream = hls::stream<SpadWritePacket>;
    using DelayedElemStream = hls::stream<DelayedElemBeat>;
    using CoreControlStream = hls::stream<CoreTileControl>;
    using SaResultStream = hls::stream<SaResultToken>;
    using DmaWordStream = hls::stream<dma_word_t>;

    inline unsigned tileCount(const unsigned length){
        #pragma HLS INLINE
        return (length+(unsigned)SA_COLS-1U)/(unsigned)SA_COLS;
    }

    inline unsigned keyTileCountForQuery(
        const unsigned query_tile,
        const unsigned tiles,
        const bool causal
    ){
        #pragma HLS INLINE
        return causal ? query_tile+1U : tiles;
    }

    enum class PeWaveOp : std::uint8_t{
        IDLE = 0,
        QK = 1,
        SUB_MAX = 2,
        SCALE = 3,
        PWL = 4,
        ROW_SUM = 5,
        PV = 6
    };

    constexpr int maxConstexpr(const int a, const int b){
        return a>b ? a : b;
    }

    constexpr int unsignedWidth(const unsigned value){
        return value<=1U ? 1 : 1+unsignedWidth(value>>1);
    }

    constexpr int PE_WAVE_ITEM_COUNT = maxConstexpr(
        maxConstexpr(SA_ROWS, SA_COLS), exp2PWLPieces
    );
    constexpr int PE_WAVE_INDEX_WIDTH = unsignedWidth(
        (unsigned)(PE_WAVE_ITEM_COUNT-1)
    );
    using PeWaveIndex = ap_uint<PE_WAVE_INDEX_WIDTH>;

    enum class CmpWaveOp : std::uint8_t{
        HOLD = 0,
        UPDATE = 1,
        PROP_MAX = 2,
        PROP_MAX_DIFF = 3,
        PROP_ZERO = 4,
        RESET = 5,
        PROP_EXP2_INTERCEPTS = 6
    };

    constexpr int PE_TOKEN_LATENCY = 9;
    // spatialPeCell的综合流水延迟为9拍。暂时恢复历史验证过的7拍
    // 调度余量，使同一环形槽每16拍才复用；本次只改变hop，保留
    // 当前已经通过RTL CoSim的数据流和真实依赖约束，作为单变量对照。
    constexpr int PE_SCHEDULER_GUARD_CYCLES = 7;
    constexpr int PE_HOP_CYCLES =
        PE_TOKEN_LATENCY+PE_SCHEDULER_GUARD_CYCLES;
    // 当前综合中每列CMP输出通路为3拍、II=1；Chisel SystolicArray还在
    // CMP.d_output与首行PE.u_input之间使用一级Pipe。HLS用4拍环形token
    // 通道表达这两个边界，保持单CMP吞吐而不把CMP和PE组合串联。
    constexpr int CMP_TOKEN_LATENCY = 3;
    constexpr int CMP_TO_PE_REGISTER_CYCLES = 1;
    constexpr int CMP_HOP_CYCLES =
        CMP_TOKEN_LATENCY+CMP_TO_PE_REGISTER_CYCLES;
    // InputDelayer依次发送Q、K、V三个错拍phase。SA不再等三个phase全部
    // 恢复成tile后才启动；Q直接写入PE.reg，K在第二个phase结束后即可
    // 发射QK，而V与QK前几拍重叠装入operand cache。
    constexpr int INPUT_DELAYER_PHASE_CYCLES = SA_COLS+SA_ROWS-1;
    constexpr int INPUT_DELAYER_TILE_CYCLES =
        3*INPUT_DELAYER_PHASE_CYCLES;
    constexpr int QK_START = 2*INPUT_DELAYER_PHASE_CYCLES;
    constexpr int FIRST_SCORE =
        QK_START+SA_ROWS*PE_HOP_CYCLES;
    constexpr int SCORES_READY = FIRST_SCORE+2*SA_COLS-2+
        CMP_HOP_CYCLES;
    constexpr int MAX_DIFF_CYCLE = SCORES_READY+1;
    constexpr int SUB_MAX_CYCLE = MAX_DIFF_CYCLE+1;
    // PROP_MAX先经过3拍CMP流水和一级CMP->PE寄存器，再等待PE结果提交，
    // SCALE必须在提交后的下一拍才读取更新后的PE.reg。
    constexpr int SCALE_CYCLE =
        SUB_MAX_CYCLE+CMP_HOP_CYCLES+
        PE_HOP_CYCLES+1;
    // CMP可以在SCALE提交前启动；4拍CMP通道把PWL数据对齐到
    // SCALE写回后的下一拍，不复制任何算术单元。
    constexpr int PWL_START = SCALE_CYCLE+PE_HOP_CYCLES+1-
        CMP_HOP_CYCLES;
    constexpr int PWL_END = PWL_START+exp2PWLPieces-1;
    constexpr int ROW_SUM_CYCLE =
        PWL_END+PE_HOP_CYCLES+1;
    // ROW_SUM也来自CMP通道，PV要到下一拍才能占用首行PE入口。
    constexpr int PV_START = ROW_SUM_CYCLE+
        CMP_HOP_CYCLES+1;
    constexpr int LAST_RESULT_CYCLE =
        PV_START+SA_ROWS-1+SA_ROWS*PE_HOP_CYCLES;
    constexpr int SA_TILE_CYCLES = LAST_RESULT_CYCLE+1;

    struct SaCycleControl{
        bool launch_qk = false;
        PeWaveIndex qk_index = 0;
        bool cmp_valid = false;
        CmpWaveOp cmp_op = CmpWaveOp::HOLD;
        PeWaveIndex cmp_item = 0;
        bool launch_down = false;
        PeWaveOp down_op = PeWaveOp::IDLE;
        PeWaveIndex down_item = 0;
    };

    using SaCycleControlStream = hls::stream<SaCycleControl>;

    void fsaCoreControllerProcess(
        unsigned length, bool causal, CoreControlStream& control_stream
    );
    void saExecutionPlanProcess(
        unsigned length, bool causal,
        SaCycleControlStream& cycle_control_stream
    );
    void dmaQRequestProcess(
        unsigned length,
        DmaReadRequestStream& q_request_stream
    );
    void dmaKRequestProcess(
        unsigned length, bool causal,
        DmaReadRequestStream& k_request_stream
    );
    void dmaVRequestProcess(
        unsigned length, bool causal,
        DmaReadRequestStream& v_request_stream
    );
    void dmaReadQ(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        unsigned length,
        DmaReadRequestStream& request_stream,
        SpadWriteStream& q_dma_stream
    );
    void dmaReadK(
        const dma_word_t k_address[DMA_MAX_QKV_WORDS],
        unsigned length, bool causal,
        DmaReadRequestStream& request_stream,
        SpadWriteStream& k_dma_stream
    );
    void dmaReadV(
        const dma_word_t v_address[DMA_MAX_QKV_WORDS],
        unsigned length, bool causal,
        DmaReadRequestStream& request_stream,
        SpadWriteStream& v_dma_stream
    );
    void scratchpadProcess(
        unsigned length, bool causal,
        SpadWriteStream& q_dma_stream,
        SpadWriteStream& k_dma_stream,
        SpadWriteStream& v_dma_stream,
        CoreControlStream& control_in,
        CoreControlStream& control_out,
        ElemRowStream& q_sa_stream,
        ElemRowStream& k_sa_stream,
        ElemRowStream& v_sa_stream
    );
    void inputDelayerProcess(
        unsigned length, bool causal,
        CoreControlStream& control_in,
        ElemRowStream& q_spad_stream,
        ElemRowStream& k_spad_stream,
        ElemRowStream& v_spad_stream,
        CoreControlStream& control_out,
        DelayedElemStream& delayed_sa_stream
    );
    void systolicArrayProcess(
        unsigned length, bool causal,
        CoreControlStream& control_stream,
        SaCycleControlStream& cycle_control_stream,
        DelayedElemStream& delayed_sa_stream,
        SaResultStream& sa_result_stream
    );
    void outputDelayerProcess(
        unsigned length, bool causal,
        SaResultStream& raw_result_stream,
        SaResultStream& aligned_result_stream
    );
    void accumulatorProcess(
        unsigned length, bool causal,
        SaResultStream& sa_result_stream,
        DmaWordStream& output_word_stream
    );
    void dmaWriteO(
        dma_word_t o_address[DMA_MAX_O_WORDS],
        unsigned length,
        DmaWordStream& output_word_stream,
        ap_uint<8>& status
    );
    void fsaStreamingDataflow(
        const dma_word_t q_address[DMA_MAX_QKV_WORDS],
        const dma_word_t k_address[DMA_MAX_QKV_WORDS],
        const dma_word_t v_address[DMA_MAX_QKV_WORDS],
        dma_word_t o_address[DMA_MAX_O_WORDS],
        unsigned length,
        bool causal,
        ap_uint<8>& status
    );

}  // namespace streaming_v2_detail
}  // namespace fsa

#endif  // FSA_STREAM_COMMON_HPP
