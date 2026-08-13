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
#include <ap_int.h>
#include <hls_half.h>

#include "fsa/config.hpp"

namespace fsa{

    /// @brief 元素精度，水平方向数据
    using elem_t = half;

    /// @brief 累加精度，竖直方向数据
    using acc_t = float;

    /// @brief 片上SRAM行地址
    using sram_address_t = ap_uint<5>;
    /// @brief 内存地址
    using memory_address_t = std::uint64_t;
    /// @brief 地址访问步长
    using sram_stride_t = ap_int<5>;
    using memory_stride_t = ap_int<21>;
    /// @brief 硬件计数器
    using exp2_counter_t = ap_uint<3>;
    using reciprocal_iter_count_t = ap_uint<4>;
    // TODO: mem_addr 根据AXI修改。无符号

    /// @brief 信号量编号
    using semaphore_id_t = ap_uint<5>;
    /// @brief 信号值
    using semaphore_value_t = ap_uint<3>;

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

    /// @brief Decoupled数据，valid&&ready时传输成功，valid=true时数据有效
    /// @tparam T 数据类型
    template <typename T>
    struct DecoupledData{
        bool valid = false;
        bool ready = false;
        T bits{};
    };

    /// @brief 信号量
    struct Semaphore{
        semaphore_id_t id = 0;
        semaphore_value_t value = 0;
    };

    /// @brief 产生一个valid=false的数据
    /// @tparam T 数据类型
    /// @return 生成的传输值
    template <typename T>
    inline ValidData<T> make_invalid(){
        return ValidData<T>{false, T{}};
    }

    /// @brief 产生一个valid=true的数据
    /// @tparam T 数据类型
    /// @param value 需要被包装的数据
    /// @return 生成的传输值
    template <typename T>
    inline ValidData<T> make_valid(const T& value){
        return ValidData<T>{true, value};
    }

}  // namespace fsa
#endif // !TYPES_HPP