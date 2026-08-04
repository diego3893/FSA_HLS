/**
 * @file config.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief FSA工程编译期参数定义，如数组大小、模块数量等
 * @date 2026-08-04
 * 
 * 
 */
#ifndef CONFIG_HPP
#define CONFIG_HPP

namespace fsa{

constexpr int SA_ROWS = 4;
constexpr int SA_COLS = 4;
constexpr int SPAD_ROWS = 2*SA_COLS+4*SA_ROWS; // spad存储深度，QKV*2
constexpr int ACC_ROWS = 1+SA_ROWS; // 1行存L，rows行存O

// 三个DMA参数，暂时不管
constexpr int nMemPorts = 4;
constexpr int dmaLoadInflight = 16;
constexpr int dmaStoreInflight = 8;

constexpr int exp2PwlPieces = 8;

constexpr int N_SEMAPHORES = 32; // 信号数组大小

// TODO: 确定除法延迟
constexpr int reciprocalLatency = -1;
}  // namespace fsa

#endif // !CONFIG_HPP