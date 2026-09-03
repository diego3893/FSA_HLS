/**
 * @file stream_cmp.hpp
 * @brief 流式FSA的逐query最大值和score反馈模块。
 */
#ifndef STREAM_CMP_HPP
#define STREAM_CMP_HPP

#include "fsa/hls/fsa_core_request_top.hpp"
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
     * @brief 4个持久化CMP处理一个QK结果波，并生成向下反馈的score。
     *
     * oldMax/newMax属于CMP模块本地状态；initialize开始新在线softmax
     * 序列。反馈矩阵随后沿PE的上到下通路装入同一组PE寄存器。
     */
    void stream_cmp_request(
        const FsaCoreRequestInput& input,
        const acc_t scores[SA_COLS][SA_ROWS],
        elem_t score_feedback[SA_COLS][SA_ROWS],
        acc_t new_max[SA_COLS],
        acc_t max_difference[SA_COLS]
    );

}  // namespace fsa

#endif  // STREAM_CMP_HPP
