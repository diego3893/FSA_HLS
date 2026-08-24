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

#include <cstdint>

#ifndef FSA_SA_ROWS
#define FSA_SA_ROWS 4
#endif

#ifndef FSA_SA_COLS
#define FSA_SA_COLS 4
#endif

#ifndef FSA_MAX_SEQUENCE_LENGTH
#define FSA_MAX_SEQUENCE_LENGTH 4096
#endif

#ifndef FSA_DMA_AXI_QKV_DEPTH
#define FSA_DMA_AXI_QKV_DEPTH \
    ((FSA_MAX_SEQUENCE_LENGTH*FSA_SA_ROWS)/4)
#endif

#ifndef FSA_DMA_AXI_O_DEPTH
#define FSA_DMA_AXI_O_DEPTH \
    ((FSA_MAX_SEQUENCE_LENGTH*FSA_SA_ROWS)/2)
#endif

namespace fsa{
    /**
     * @brief SA行数，也是attention的head dimension。
     *
     * 可在编译参数中使用-DFSA_SA_ROWS=128覆盖默认值。
     */
    constexpr int SA_ROWS = FSA_SA_ROWS;

    /**
     * @brief SA列数，也是系统顶层一次处理的query/key token数量。
     *
     * 矩形配置128x4表示head_dim=128，每个序列tile包含4个token。
     */
    constexpr int SA_COLS = FSA_SA_COLS;

    /// @brief 单次系统顶层事务允许的最大序列长度。
    constexpr int MAX_SEQUENCE_LENGTH = FSA_MAX_SEQUENCE_LENGTH;

    /// @brief 当前AXI接口depth声明覆盖到的最大head dimension。
    constexpr int MAX_SUPPORTED_HEAD_DIM = 128;

    /// @brief 当前AXI接口depth声明覆盖到的最大序列长度。
    constexpr int MAX_SUPPORTED_SEQUENCE_LENGTH = 4096;

    static_assert(SA_ROWS>0, "SA_ROWS必须大于0");
    static_assert(SA_COLS>0, "SA_COLS必须大于0");
    static_assert(MAX_SEQUENCE_LENGTH>0, "最大序列长度必须大于0");
    static_assert(
        SA_ROWS<=MAX_SUPPORTED_HEAD_DIM,
        "如需head_dim大于128，必须同步增大DMA AXI depth上限"
    );
    static_assert(
        MAX_SEQUENCE_LENGTH<=MAX_SUPPORTED_SEQUENCE_LENGTH,
        "如需L上限大于4096，必须同步增大DMA AXI depth上限"
    );

    namespace detail{

        /**
         * @brief 编译期平方根，仅用于生成硬件配置常量
         *
         * SA_ROWS是编译参数，因此该函数只会被C++编译器求值，不会进入
         * Vitis HLS数据通路。固定迭代次数避免依赖非constexpr的sqrt实现。
         */
        constexpr double compileTimeSqrt(const double value){
            double estimate = value>=1.0 ? value : 1.0;
            for(int iteration=0; iteration<32; ++iteration){
                estimate = 0.5*(estimate+value/estimate);
            }
            return estimate;
        }

        /**
         * @brief 把正的正常double常量按RNE编码成IEEE-754 binary16
         *
         * 返回位模式而不是在HLS函数中执行FP32到FP16转换，保证
         * attentionScale的元素精度值也完全由编译器生成。
         */
        constexpr std::uint16_t positiveNormalFp16Bits(double value){
            int exponent = 0;
            while(value>=2.0){
                value *= 0.5;
                ++exponent;
            }
            while(value<1.0){
                value *= 2.0;
                --exponent;
            }

            const double exact_fraction = (value-1.0)*1024.0;
            unsigned fraction = static_cast<unsigned>(exact_fraction);
            const double remainder = exact_fraction-(double)fraction;
            const bool round_up = remainder>0.5 ||
                (remainder==0.5 && (fraction&1U)!=0U);
            if(round_up){
                ++fraction;
            }
            if(fraction==1024U){
                fraction = 0;
                ++exponent;
            }

            return static_cast<std::uint16_t>(
                ((unsigned)(exponent+15)<<10) | fraction
            );
        }

    }  // namespace detail

    /**
     * @brief softmax指数换底和head-dimension缩放常量
     *
     * exp2((score-max)*ATTENTION_SCALE)等价于
     * exp((score-max)/sqrt(SA_ROWS))。初始化表达式必须是常量表达式，
     * 所以综合输入中不会出现sqrt或除法计算。
     */
    constexpr double ATTENTION_SCALE_EXACT =
        1.4426950408889634074/detail::compileTimeSqrt((double)SA_ROWS);
    constexpr float ATTENTION_SCALE_ACC_VALUE =
        static_cast<float>(ATTENTION_SCALE_EXACT);
    constexpr std::uint16_t ATTENTION_SCALE_ELEM_BITS =
        detail::positiveNormalFp16Bits(ATTENTION_SCALE_EXACT);

    static_assert(
        ATTENTION_SCALE_EXACT>=0.00006103515625 &&
        ATTENTION_SCALE_EXACT<65504.0,
        "attentionScale必须位于FP16正常数范围内"
    );

    /// @brief Scratchpad SRAM行数
    constexpr int SPAD_ROWS = 2*SA_COLS+4*SA_ROWS;

    /// @brief Accumulator SRAM行数
    constexpr int ACC_ROWS = 1 + SA_ROWS;

    /// @brief Scratchpad SRAM的物理bank数量
    constexpr int spadBanks = 2;

    /// @brief Accumulator SRAM的物理bank数量
    constexpr int accBanks = 2;

    /// @brief BankedSRAM窄端口一次传输的字节数
    constexpr int beatBytes = 8;

    /// @brief elem_t的硬件位宽
    constexpr int elemWidth = 16;

    /// @brief acc_t的硬件位宽
    constexpr int accWidth = 32;

    /// @brief Scratchpad一整行包含的sub-bank数量
    constexpr int SPAD_SUB_BANKS = (SA_ROWS*elemWidth)/(beatBytes*8);

    /// @brief Accumulator SRAM一整行包含的sub-bank数量
    constexpr int ACC_SUB_BANKS = (SA_COLS*accWidth)/(beatBytes*8);

    static_assert(
        (SA_ROWS*elemWidth)%(beatBytes*8)==0,
        "Scratchpad行宽必须能被DMA beat宽度整除"
    );

    static_assert(
        (SA_COLS*accWidth)%(beatBytes*8)==0,
        "Accumulator SRAM行宽必须能被DMA beat宽度整除"
    );

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
