/**
 * @file split_d_types.hpp
 * @brief Split-D方形阵列中的PE状态。
 */
#ifndef SPLIT_D_TYPES_HPP
#define SPLIT_D_TYPES_HPP

#include "fsa/stream/arithmetic.hpp"
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
        unsigned active_queries;
        unsigned key_tiles;
        unsigned length;
        bool causal;
    };

    /// @brief 相同key/feature的K和V，由唯一DMA分发器广播至块内tile缓存。
    struct KeyValuePacket{
        elem_t key;
        elem_t value;
    };

}  // namespace split_d
}  // namespace fsa

#endif  // SPLIT_D_TYPES_HPP
