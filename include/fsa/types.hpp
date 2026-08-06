/**
 * @file types.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 工程内部类型声明
 * @date 2026-08-05
 * 
 * 
 */
#ifndef TYPES_HPP
#define TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "fsa/config.hpp"

namespace fsa{

/// @brief 元素精度，水平方向数据
using elem_t = float;

/// @brief 累加精度，竖直方向数据
using acc_t = float;
// TODO: elem_t FP16，acc_t FP32

/// @brief 片上SRAM行地址
using sram_address_t = std::uint32_t;
/// @brief 内存地址
using memory_address_t = std::uint64_t;
/// @brief 地址访问步长
using sram_stride_t = std::int32_t;
using memory_stride_t = std::int32_t;
/// @brief 硬件计数器
using exp2_counter_t = std::uint32_t;
using reciprocal_counter_t = std::uint32_t;
// TODO: sram_addr 5位（pad和acc SRAM都用这个），mem_addr 根据AXI修改。无符号
// TODO: sram_stride 5位，mem_stride 21位。有符号
// TODO: exp2 3位和reciprocal 根据延迟定位数。无符号

/// @brief 定长向量
/// @tparam T 数据类型
/// @tparam N 向量长度
template <typename T, std::size_t N>
using FixedVector = std::array<T, N>;

/// @brief elem_t类型的定长向量
using ElemVector = FixedVector<elem_t, (std::size_t)SA_ROWS>;

/// @brief acc_t类型的定长向量
using AccVector = FixedVector<acc_t, (std::size_t)SA_COLS>;

/// @brief Valid数据，valid=true时数据有效
/// @tparam T 数据类型
template <typename T>
struct ValidData{
    bool valid = false;
    T bits{};
};

/// @brief Decoupled数据，valid&&ready时传输成功，valid=T时数据有效
/// @tparam T 数据类型
template <typename T>
struct DecoupledData{
    bool valid = false;
    bool ready = false;
    T bits{};
};

/// @brief 信号量
struct Semaphore{
    std::uint8_t id = 0;
    std::uint8_t value = 0;
    // TODO: id5位，value3位
};

/// @brief 产生一个valid=F的数据
/// @tparam T 数据类型
/// @return 生成的传输值
template <typename T>
inline ValidData<T> make_invalid(){
    return ValidData<T>{false, T{}};
}

/// @brief 产生一个valid=T的数据
/// @tparam T 数据类型
/// @param value 需要被包装的数据
/// @return 生成的传输值
template <typename T>
inline ValidData<T> make_valid(const T& value){
    return ValidData<T>{true, value};
}

}  // namespace fsa
#endif // !TYPES_HPP