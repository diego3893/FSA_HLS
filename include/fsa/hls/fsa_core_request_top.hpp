/**
 * @file fsa_core_request_top.hpp
 * @brief 接收一个Q/K/V tile并自行完成在线FlashAttention的请求级顶层
 */
#ifndef FSA_CORE_REQUEST_TOP_HPP
#define FSA_CORE_REQUEST_TOP_HPP

#include "fsa/config.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief 一个KV block请求。
     *
     * Q布局为[query][feature]，K为[key][feature]，V为[key][value_feature]。
     * initialize=true开始一条新的在线softmax序列；后续KV block使用false，
     * 以复用内部CMP max以及accRAM中的非零L/O。finalize=true表示最后一个
     * KV block，并在写回前执行O/L归一化。
     */
    struct FsaCoreRequestInput{
        bool reset = false;
        bool request_valid = false;
        bool initialize = true;
        bool finalize = true;
        bool causal = false;

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

        /// @brief 在线softmax分母，lane顺序对应query。
        acc_t l[SA_COLS]{};

        /**
         * @brief attention输出，布局为[query][value_feature]。
         *
         * normalized=false时是尚未除以L的在线累计分子；finalize=true后
         * normalized=true，此处为最终O/L。
         */
        acc_t o[SA_COLS][SA_ROWS]{};
    };

}  // namespace fsa

void fsa_core_request_top(
    const fsa::FsaCoreRequestInput& input,
    fsa::FsaCoreRequestOutput& output
);

#endif  // FSA_CORE_REQUEST_TOP_HPP
