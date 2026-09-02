/**
 * @file stream_array.hpp
 * @brief 与Scala FSA数据通路等价的持久化流式阵列接口。
 */
#ifndef STREAM_ARRAY_HPP
#define STREAM_ARRAY_HPP

#include "fsa/hls/fsa_core_request_top.hpp"

namespace fsa{

    /**
     * @brief 用一套持久化PE/CMP网络处理一个KV tile。
     *
     * PE本地reg依次保存Q、S、N和P。score沿下到上路径进入CMP，CMP
     * 更新最大值后沿上到下路径把score/max/截距送回同一组PE；P直接
     * 留在PE中供rowsum和PV复用，不存在阵列外的S/P phase bank。
     */
    void stream_fsa_tile(
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
