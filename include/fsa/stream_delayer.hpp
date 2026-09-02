/**
 * @file stream_delayer.hpp
 * @brief FSA DATAFLOW边界的流式输入/输出对齐网络。
 */
#ifndef STREAM_DELAYER_HPP
#define STREAM_DELAYER_HPP

#include "fsa/hls/fsa_core_request_top.hpp"
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
        acc_t reduction_result[SA_COLS][SA_ROWS],
        elem_t lane_result[SA_ROWS][SA_COLS],
        acc_t scalar_reduction[SA_COLS]
    );

    /**
     * @brief 固定FSA请求调度与InputDelayer组成的DATAFLOW输入任务。
     *
     * 这里只展开LOAD/SCORE/VALUE/NORM固定波形，不实现指令队列、冲突
     * 检查或controller指令重叠。
     */
    void stream_fsa_input_delayer_process(
        const FsaCoreRequestInput& input,
        StreamArrayCycleStream& output
    );

    /**
     * @brief 对齐阵列底部各列并送往Accumulator。
     */
    void stream_fsa_output_delayer_process(
        StreamArrayOutputStream& input,
        StreamArrayOutputStream& output
    );

}  // namespace fsa

#endif  // STREAM_DELAYER_HPP
