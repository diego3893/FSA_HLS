/**
 * @file stream_delayer.hpp
 * @brief FSA DATAFLOW边界的流式输入/输出对齐网络。
 */
#ifndef STREAM_DELAYER_HPP
#define STREAM_DELAYER_HPP

#include "fsa/stream_types.hpp"

namespace fsa{

    void stream_input_delayer(
        const elem_t data[SA_COLS][SA_ROWS],
        const acc_t column_operand[SA_COLS],
        std::uint16_t active_keys,
        StreamPeOp op,
        int wave_count,
        StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1],
        StreamPeTokenStream vertical[SA_ROWS+1][SA_COLS]
    );

    void stream_output_delayer(
        StreamPeOp op,
        int wave_count,
        StreamPeTokenStream horizontal[SA_ROWS][SA_COLS+1],
        StreamPeTokenStream vertical[SA_ROWS+1][SA_COLS],
        StreamPeLaneStream lane[SA_ROWS][SA_COLS],
        const bool lane_enabled[SA_ROWS][SA_COLS],
        bool initialize,
        const acc_t old_max[SA_COLS],
        acc_t reduction_result[SA_COLS][SA_ROWS],
        elem_t lane_result[SA_ROWS][SA_COLS],
        acc_t scalar_reduction[SA_COLS],
        acc_t new_max[SA_COLS],
        acc_t max_difference[SA_COLS]
    );

}  // namespace fsa

#endif  // STREAM_DELAYER_HPP
