/**
 * @file stream_array.hpp
 * @brief 持久PE阵列完成QK、原地S-N-P、rowsum和PV的tile接口。
 */
#ifndef STREAM_ARRAY_HPP
#define STREAM_ARRAY_HPP

#include "fsa/hls/fsa_core_request_top.hpp"
#include "fsa/stream_pe.hpp"

namespace fsa{

    /**
     * @brief 跨KV tile保留的在线softmax状态。
     */
    struct StreamOnlineState{
        bool active = false;
        acc_t m[SA_COLS]{};
        acc_t l[SA_COLS]{};
        acc_t o[SA_COLS][SA_ROWS]{};
    };

    void reset_stream_online_state(StreamOnlineState& state);

    /**
     * @brief 用一套PE阵列处理一个KV tile。
     *
     * QK后score经CMP求max并回灌；N和P只写回同一PE的reg，随后直接
     * 被rowsum/PV消费，不将完整P tile写入外部SRAM。
     */
    void stream_fsa_tile(
        StreamOnlineState& online,
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    );

    /**
     * @brief 与旧fsa_core_request_run并列的流式请求入口。
     */
    void fsa_stream_request_run(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    );

}  // namespace fsa

#endif  // STREAM_ARRAY_HPP
