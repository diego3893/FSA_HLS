/**
 * @file config.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 定义FSA-HLS的编译期硬件规模参数
 * @date 2026-08-04
 * 
 * 
 */
#ifndef CONFIG_HPP
#define CONFIG_HPP

namespace fsa{
    /// @brief SA行数
    constexpr int SA_ROWS = 4;

    /// @brief SA列数
    constexpr int SA_COLS = 4;

    /// @brief Scratchpad SRAM行数
    constexpr int SPAD_ROWS = 2*SA_COLS+4*SA_ROWS;

    /// @brief Accumulator SRAM行数
    constexpr int ACC_ROWS = 1 + SA_ROWS;

    /// @brief 外部内存访问端口数
    constexpr int nMemPorts = 4;

    /// @brief LoadQueue最多保存请求数
    constexpr int dmaLoadInflight = 16;

    /// @brief StoreQueue最多保存请求数
    constexpr int dmaStoreInflight = 8;

    /// @brief PWL区间数
    constexpr int exp2PWLPieces = 8;

    /// @brief 信号量槽位数量
    constexpr int N_SEMAPHORES = 32;

    /// @brief 恢复除法器每拍组合产生的商位数
    constexpr int reciprocalBitsPerCycle = 2;

    /**
     * @brief reciprocal为RNE舍入生成的商位数
     *
     * FP32有效数共24位（含隐藏位），再保留guard和round两位。
     * 最终没有被商寄存器保存的余数用于产生sticky位。
     */
    constexpr int reciprocalQuotientBits = 24+2;

    /// @brief 恢复除法的迭代拍数，当前为ceil(26/2)=13
    constexpr int reciprocalIterationCycles =
        (reciprocalQuotientBits+reciprocalBitsPerCycle-1)
        /reciprocalBitsPerCycle;

    /**
     * @brief 从接收请求到结果有效所占用的固定控制窗口
     *
     * 1拍IDLE接收请求 + 13拍ITER + 1拍DONE规格化并舍入。
     */
    constexpr int reciprocalLatency = 1+reciprocalIterationCycles+1;

    static_assert(
        reciprocalQuotientBits%reciprocalBitsPerCycle==0,
        "当前恢复除法器要求商位数能被每拍商位数整除"
    );

}  // namespace fsa
#endif // !CONFIG_HPP
