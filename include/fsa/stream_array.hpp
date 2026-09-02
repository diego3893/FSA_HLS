/**
 * @file stream_array.hpp
 * @brief 一组共享FMA PE阵列完成QK、S-N-P、rowsum和PV的tile接口。
 */
#ifndef STREAM_ARRAY_HPP
#define STREAM_ARRAY_HPP

#include "fsa/hls/fsa_core_request_top.hpp"

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
     * QK后score经CMP求max并写入完全分割的PE resident bank；各phase
     * 顺序复用同一组FMA mesh，P随后直接被rowsum/PV消费，不进入外部SRAM。
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
