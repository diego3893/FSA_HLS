/**
 * @file pe_raw_fma.hpp
 * @brief PE专用混合精度Raw FMA的独立阶段一接口
 */
#ifndef PE_RAW_FMA_HPP
#define PE_RAW_FMA_HPP

#include "fsa/stream/types.hpp"

namespace fsa{

    /**
     * @brief Raw FMA输入位模式
     *
     * 普通模式把两个FP16输入与一个FP32累加数做融合乘加；exp2模式
     * 复用同一个尾数乘法器，并按现有PE协议解释编码截距。
     */
    struct PeRawFmaInput{
        ap_uint<16> in_a_bits = 0;
        ap_uint<16> in_b_bits = 0;
        ap_uint<32> in_c_bits = 0;
        bool in_exp2 = false;
    };

    /** @brief Raw FMA同时产生FP32、FP16和PWL命中结果。 */
    struct PeRawFmaOutput{
        ap_uint<32> out_acc_bits = 0;
        ap_uint<16> out_elem_bits = 0;
        bool out_exp2 = false;
    };

    /**
     * @brief 只用定宽整数位域实现PE的混合精度FMA
     *
     * 该函数是阶段一独立候选，尚未接入正式SA。综合验收目标是
     * latency不超过5、II=1，并且只生成一条尾数乘加数据通路。
     */
    PeRawFmaOutput peRawFma(const PeRawFmaInput& input);

}  // namespace fsa

/** @brief 阶段一独立Vitis HLS顶层。 */
void pe_raw_fma_top(
    const fsa::PeRawFmaInput& input,
    fsa::PeRawFmaOutput& output
);

#endif  // PE_RAW_FMA_HPP
