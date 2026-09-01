/**
 * @file fsa_stream_request_top.hpp
 * @brief 高吞吐流式FSA请求核HLS顶层。
 */
#ifndef FSA_STREAM_REQUEST_TOP_HPP
#define FSA_STREAM_REQUEST_TOP_HPP

#include "fsa/hls/fsa_core_request_top.hpp"

void fsa_stream_request_top(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
);

#endif  // FSA_STREAM_REQUEST_TOP_HPP
