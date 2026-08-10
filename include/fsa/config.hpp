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

    /// @brief 倒数计算的延迟
    constexpr int reciprocalLatency = -1;
    // TODO: 延迟未确定

}  // namespace fsa
#endif // !CONFIG_HPP