/**
 * @file fsa_core_request_top.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 接收一个Q/K/V tile并自行完成在线FlashAttention的请求级顶层
 * @date 2026-08-25
 * 
 * 
 */
#ifndef FSA_CORE_REQUEST_TOP_HPP
#define FSA_CORE_REQUEST_TOP_HPP

#include "fsa/config.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief 一个KV block请求。
     *
     */
    struct FsaCoreRequestInput{
        bool reset = false;
        bool request_valid = false;
        bool initialize = true; // 是否为第一个块，涉及reset
        bool finalize = true; // 是否为最后一个块，涉及归一化
        bool causal = false;

        /// @brief k/v前active_keys行有效，范围为1..SA_COLS。
        std::uint16_t active_keys = SA_COLS;

        /// @brief 用于跨tile causal mask的全局序列下标。
        std::uint32_t query_base = 0;
        std::uint32_t key_base = 0;

        elem_t q[SA_COLS][SA_ROWS]{};
        elem_t k[SA_ROWS][SA_ROWS]{};
        elem_t v[SA_ROWS][SA_ROWS]{};
    };

    struct FsaCoreRequestOutput{
        bool request_ready = false;
        bool request_done = false;
        bool protocol_error = false;
        bool normalized = false;
        ap_uint<16> executed_steps = 0;

        /// @brief 在线softmax分母，lane顺序对应query
        acc_t l[SA_COLS]{};

        /**
         * @brief attention输出，布局为[query][value_feature]
         *
         */
        acc_t o[SA_COLS][SA_ROWS]{};
    };

    /**
     * @brief 不带外部接口pragma的请求核入口，供不同HLS系统顶层复用
     *
     */
    void fsa_core_request_run(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    );

}  // namespace fsa

void fsa_core_request_top(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
);

#endif  // FSA_CORE_REQUEST_TOP_HPP
