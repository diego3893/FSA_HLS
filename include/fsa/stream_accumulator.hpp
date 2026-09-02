/**
 * @file stream_accumulator.hpp
 * @brief 流式FSA跨KV tile保存L/O并完成最终归一化的Accumulator。
 */
#ifndef STREAM_ACCUMULATOR_HPP
#define STREAM_ACCUMULATOR_HPP

#include "fsa/hls/fsa_core_request_top.hpp"

namespace fsa{

    void stream_accumulator_update(
        bool initialize,
        bool finalize,
        const acc_t max_difference[SA_COLS],
        const acc_t rowsum[SA_COLS],
        const acc_t pv[SA_COLS][SA_ROWS],
        acc_t online_l[SA_COLS],
        acc_t online_o[SA_COLS][SA_ROWS],
        FsaCoreRequestOutput& output
    );

}  // namespace fsa

#endif  // STREAM_ACCUMULATOR_HPP
