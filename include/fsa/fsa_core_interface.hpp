/**
 * @file fsa_core_interface.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明FSA Core控制面与数据通路共用的请求类型
 * @date 2026-08-25
 *
 * 这些类型由ExecutionPlan或其他控制逻辑产生，由FSA Core数据通路消费。
 * 它们不属于任何特定Vitis HLS顶层，因此放在core层公共接口中。
 */
#ifndef FSA_CORE_INTERFACE_HPP
#define FSA_CORE_INTERFACE_HPP

#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief 发往Scratchpad整行读端口的一个logical step请求
     *
     */
    struct SpReadRequest{
        bool valid = false;
        bool is_constant = false;
        sram_address_t addr = 0;
        bool rev_sram_out = false;
        bool delay_sram_out = false;
        bool rev_delayer_out = false;
    };

    /**
     * @brief 发往Accumulator SRAM整行读端口的一个logical step请求
     *
     */
    struct AccReadRequest{
        bool valid = false;
        bool is_constant = false;
        sram_address_t addr = 0;
        bool rmw = false;
    };

}  // namespace fsa

#endif  // FSA_CORE_INTERFACE_HPP
