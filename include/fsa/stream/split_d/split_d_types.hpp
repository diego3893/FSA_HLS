/**
 * @file split_d_types.hpp
 * @brief Split-D方形阵列中的PE状态。
 */
#ifndef SPLIT_D_TYPES_HPP
#define SPLIT_D_TYPES_HPP

#include "fsa/stream/arithmetic.hpp"
#include "fsa/stream/dma.hpp"
#include "fsa/stream/split_d/split_d_config.hpp"

namespace fsa{
namespace split_d{

    /**
     * @brief 一个Split-D PE拥有的算法状态。
     *
     * reg依次保存当前Q元素和softmax概率P。score_acc是新增的FP32
     * 寄存器，在全部DIM_BLOCKS轮QK完成前持续保存同一个S元素。
     */
    struct PeState{
        elem_t reg{};
        acc_t score_acc{};
    };

    /// @brief 分发器送给每块的query-tile头，计数决定全部有限数据的消费量。
    struct QueryTileHeader{
        unsigned query_base;
        unsigned context_count;
        unsigned active_queries;
        unsigned key_tiles;
        unsigned length;
        bool causal;
    };

    /// @brief 同一key的两个原始64-bit K/V字；worker按DMA lane解包。
    struct KeyValuePacket{
        dma_word_t key;
        dma_word_t value;
    };

    /// @brief 所有计算phase由一个有限cycle引擎选择输入，共享PE与列Accumulator。
    enum class SharedOperation : std::uint8_t{
        QK, SUB_MAX, SCALE, PWL, ROW_SUM, PV, ALPHA_SCALE, ALPHA_PWL, NORMALIZE
    };

    /// @brief control和RawFMA结果在相同年龄环槽中推进；不是外部握手通道。
    struct SharedToken{
        bool valid;
        unsigned context;
        unsigned index;
    };

    /**
     * @brief 一个D×B worker的算法状态；context是RAM地址维，坐标维分bank。
     *
     * QK保存完整FP32 S，softmax后reg保存P。K/V按原DMA布局装载；
     * 部分和沿key行传递，逐token context选择稳定的行本地P。
     */
    struct SharedEngineState{
        elem_t q[QUERY_CONTEXTS][QUERY_BLOCK_COLS][HEAD_DIM];
        elem_t k[PE_DIM][HEAD_DIM];
        elem_t v[PE_DIM][HEAD_DIM];
        elem_t reg[QUERY_CONTEXTS][PE_DIM][QUERY_BLOCK_COLS];
        acc_t score[QUERY_CONTEXTS][PE_DIM][QUERY_BLOCK_COLS];
        bool score_valid[QUERY_CONTEXTS][PE_DIM][QUERY_BLOCK_COLS];
        bool probability_ready[QUERY_CONTEXTS][PE_DIM][QUERY_BLOCK_COLS];
        acc_t output_acc[QUERY_CONTEXTS][QUERY_BLOCK_COLS][HEAD_DIM];
        acc_t running_max[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
        acc_t running_sum[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
        acc_t next_max[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
        acc_t alpha_delta[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
        acc_t alpha[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
        acc_t inverse_sum[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
        bool history_valid[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
        bool tile_has_value[QUERY_CONTEXTS][QUERY_BLOCK_COLS];
    };

}  // namespace split_d
}  // namespace fsa

#endif  // SPLIT_D_TYPES_HPP
