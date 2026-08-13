/**
 * @file arithmetic.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 算术模块声明
 * @date 2026-08-05
 * 
 * 
 */
#ifndef ARITHMETIC_HPP
#define ARITHMETIC_HPP

#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief PE的mac计算
     * 
     * @param in_a PE.reg
     * @param in_b l_input
     * @param in_c u/d_input
     * @return acc_t a*b+c
     */
    acc_t peMac(elem_t in_a, elem_t in_b, acc_t in_c);

    /// @brief PE计算单元输出
    struct PeMacUnitOutput{
        /// @brief acc_t结果，上下方向使用
        acc_t out_accType{};

        /// @brief elem_t结果，左右方向使用
        elem_t out_elemType{};

        /// @brief PWL分段是否与x匹配，即当前PWL结果是否有效
        bool out_exp2 = false;
    };

    /**
     * @brief 执行 PE 内部 MacUnit 的普通 MAC 或 exp2 模式
     *
     * @param in_a PE.reg
     * @param in_b l_input or slope
     * @param in_c u/d_input or intercept
     * @param in_exp2 true则exp2，否则MAC
     * @return PeMacUnitOutput 两种精度的计算结果与exp2标志
     */
    PeMacUnitOutput peMacUnit(elem_t in_a, elem_t in_b, acc_t in_c, bool in_exp2);

    /**
     * @brief Acc的mac计算
     * 
     * @param in_a scale
     * @param in_b sram_in
     * @param in_c sa_in/0
     * @return acc_t a*b+c
     */
    acc_t accUnit(acc_t in_a, acc_t in_b, acc_t in_c);

    /// @brief CMP的输出字段
    struct CmpUnitOutput{
        acc_t out_max{};
        acc_t out_diff{};
    };

    /**
     * @brief CMP计算
     * 
     * @param in_a 根据CMP控制信号选择
     * @param in_b newMax
     * @return CmpUnitOutput 最大值、差值
     */
    CmpUnitOutput accCmp(acc_t in_a, acc_t in_b);

    /**
     * @brief acc_t转换为elem_t
     * 
     * @param a acc_t数据
     * @return elem_t 转换后的elem_t数据
     */
    elem_t cvtAtoE(acc_t a);

    /**
     * @brief 把elem_t放入acc_t，不做数值转换
     * 
     * @param e elem_t数据
     * @return acc_t acc_t格式数据
     */
    acc_t viewEasA(elem_t e);

    /**
     * @brief 把acc_t放入elem_t，不做数值转换
     * 
     * @param a acc_t数据
     * @return elem_t elem_t格式数据
     */
    elem_t viewAasE(acc_t a);

    /**
     * @brief PE的PWL计算
     * 
     * @param x 指数
     * @param slope 斜率
     * @param intercept 编码截距
     * @return acc_t PWL结果
     */
    acc_t peExp2PWL(elem_t x, elem_t slope, acc_t intercept);

    /**
     * @brief 取得CMP当前需要发送的PWL截距
     * @param index exp2_counter的当前值
     * @return acc_t 返回编码后的截距
     */
    acc_t exp2PWLIntercept(exp2_counter_t index);

    /**
     * @brief Acc的exp2计算
     * 
     * @param x 输入数据
     * @return acc_t exp2结果
     */
    acc_t accExp2PWL(acc_t x);

    /**
     * @brief 产生elem_t的0
     * 
     * @return elem_t 0
     */
    elem_t elemZero();

    /**
     * @brief 产生elem_t的1
     * 
     * @return elem_t 1
     */
    elem_t elemOne();

    /**
     * @brief 产生acc_t的0
     * 
     * @return acc_t 0
     */
    acc_t accZero();

    /**
     * @brief 产生acc_t的-INF
     * 
     * @return acc_t -INF
     */
    acc_t accMinimum();

    /**
     * @brief 计算attentionScale
     * 
     * @return acc_t log2(e)/sqrt(dk)
     */
    acc_t attentionScale();

}  // namespace fsa

#endif // !ARITHMETIC_HPP
