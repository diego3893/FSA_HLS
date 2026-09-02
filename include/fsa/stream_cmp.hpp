/**
 * @file stream_cmp.hpp
 * @brief 流式FSA的逐query最大值和score反馈模块。
 */
#ifndef STREAM_CMP_HPP
#define STREAM_CMP_HPP

#include "fsa/cmp.hpp"
#include "fsa/types.hpp"

namespace fsa{

    void stream_cmp_update(
        const acc_t scores[SA_COLS][SA_ROWS],
        std::uint16_t active_keys,
        bool causal,
        std::uint32_t query_base,
        std::uint32_t key_base,
        bool initialize,
        const acc_t old_max[SA_COLS],
        elem_t score_resident[SA_ROWS][SA_COLS],
        acc_t new_max[SA_COLS],
        acc_t max_difference[SA_COLS]
    );

    /**
     * @brief 持久化FSA阵列顶部一个物理CMP的一拍。
     */
    void stream_cmp_cycle(
        int instance,
        const CMPState& current,
        CMPState& next,
        CMPIO& io
    );

}  // namespace fsa

#endif  // STREAM_CMP_HPP
