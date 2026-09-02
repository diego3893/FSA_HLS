/**
 * @file stream_cmp.hpp
 * @brief 流式FSA的逐query最大值和score反馈模块。
 */
#ifndef STREAM_CMP_HPP
#define STREAM_CMP_HPP

#include "fsa/types.hpp"

namespace fsa{

    void stream_cmp_finalize_lane(
        int query,
        acc_t previous_max,
        acc_t current_max,
        acc_t& new_max,
        acc_t& max_difference
    );

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

}  // namespace fsa

#endif  // STREAM_CMP_HPP
