/** @file fp32_raw_fma.hpp
 *  @brief 已通过3A独立验收的FP32融合乘加，并供3B Accumulator复用。
 */
#ifndef FP32_RAW_FMA_HPP
#define FP32_RAW_FMA_HPP

#include <ap_int.h>

namespace fsa{

    /** @brief 三个IEEE-754 binary32输入位模式，计算a*b+c。 */
    struct Fp32RawFmaInput{
        ap_uint<32> a_bits = 0;
        ap_uint<32> b_bits = 0;
        ap_uint<32> c_bits = 0;
    };

    /**
     * @brief 单个24x24尾数乘法、完整48位乘积、最终一次RNE舍入。
     * 支持非规格化数、符号零、Inf和canonical quiet NaN；不输出异常标志。
     * 不含PWL或缩放，由Accumulator共享lane在外部选择操作数。
     */
    ap_uint<32> fp32RawFma(const Fp32RawFmaInput& input);

}  // namespace fsa

/** @brief 3A独立综合顶层，II/latency目标须由Vitis实测验收。 */
void fp32_raw_fma_top(
    const fsa::Fp32RawFmaInput& input,
    ap_uint<32>& output
);

#endif  // FP32_RAW_FMA_HPP
