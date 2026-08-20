#ifndef INSTRUCTION_HPP
#define INSTRUCTION_HPP


#include <cstdint>

#include "fsa/types.hpp"

// =============================================================================
// 文件作用
// =============================================================================
// 定义解码后的 Matrix、DMA 和 Fence 指令结构。字段名称与嵌套层次保持原 Chisel
// 形式，例如 instruction.header.func、instruction.spad.revInput、
// instruction.acc.zero，方便直接对照 isa/*.scala。
//
// 本文件不负责从原始 32 位指令字拆位，也不执行任何指令。
//
// 对应位置：
//   - InstTypes/MxFunc/DMAFunc -> isa/ISA.scala
//   - MatrixInstruction*      -> isa/MatrixInstruction.scala
//   - DMAInstruction*         -> isa/DMAInstruction.scala
//   - FenceInstruction        -> isa/FenceInstruction.scala

namespace fsa {

enum class InstTypes : std::uint8_t {
    FENCE = 0,
    MATRIX = 1,
    DMA = 2,
};

enum class MxFunc : std::uint8_t {
    LOAD_STATIONARY = 0,
    ATTENTION_SCORE_COMPUTE = 1,
    ATTENTION_VALUE_COMPUTE = 2,
    ATTENTION_LSE_NORM_SCALE = 3,
    ATTENTION_LSE_NORM = 4,
};

// 对应 MatrixInstructionHeader；HasInstructionType 和 HasSemaphore 的字段在这里
// 显式展开，字段名称仍保持原样。
struct MatrixInstructionHeader {
    InstTypes instType = InstTypes::MATRIX;
    std::uint8_t semId = 0;
    bool acquireValid = false;
    std::uint8_t acquireSemValue = 0;
    bool releaseValid = false;
    std::uint8_t releaseSemValue = 0;
    MxFunc func = MxFunc::LOAD_STATIONARY;
    bool waitPrevAcc = false;
};

struct MatrixInstructionSpad {
    sram_address_t addr = 0;
    sram_stride_t stride = 0;
    bool revInput = false;
    bool revOutput = false;
    bool delayOutput = false;
};

struct MatrixInstructionAcc {
    sram_address_t addr = 0;
    sram_stride_t stride = 0;
    bool zero = false;
    bool causal = false;
};

// 成员顺序与 MatrixInstruction.scala 一致：acc、spad、header。
struct MatrixInstruction {
    MatrixInstructionAcc acc{};
    MatrixInstructionSpad spad{};
    MatrixInstructionHeader header{};
};

enum class DMAFunc : std::uint8_t {
    LD_SRAM = 0,
    ST_SRAM = 1,
};

struct DMAInstructionHeader {
    InstTypes instType = InstTypes::DMA;
    std::uint8_t semId = 0;
    bool acquireValid = false;
    std::uint8_t acquireSemValue = 0;
    bool releaseValid = false;
    std::uint8_t releaseSemValue = 0;
    DMAFunc func = DMAFunc::LD_SRAM;
    std::uint16_t repeat = 0;
};

struct DMAInstructionSRAM {
    sram_address_t addr = 0;
    sram_stride_t stride = 0;
    bool isAccum = false;

    // getStride() 所用外部内存步长高 6 位；名称对应 Chisel 的 mem_stride1。
    std::uint32_t mem_stride1 = 0;
};

struct DMAInstructionMem {
    memory_address_t addr = 0;

    // 外部内存步长低 15 位。
    std::uint32_t stride2 = 0;
    std::uint16_t size = 0;
};

// 成员顺序与 DMAInstruction.scala 一致：mem、sram、header。
struct DMAInstruction {
    DMAInstructionMem mem{};
    DMAInstructionSRAM sram{};
    DMAInstructionHeader header{};
};

// 对应 DMAInstruction.scala 的 getStride：
//   (sram.mem_stride1 ## mem.stride2).asSInt
//
// 原字段为 6+15=21 位有符号数。这里显式掩码并符号扩展，避免普通 int 的平台
// 位宽或高位脏数据改变结果。放成 inline 是因为当前规划没有 instruction.cpp。
inline int getStride(const DMAInstruction& instruction) noexcept {
    constexpr std::uint32_t STRIDE2_BITS = 15;
    constexpr std::uint32_t STRIDE_BITS = 21;
    constexpr std::uint32_t STRIDE2_MASK = (1U << STRIDE2_BITS) - 1U;
    constexpr std::uint32_t STRIDE1_MASK = (1U << (STRIDE_BITS - STRIDE2_BITS)) - 1U;
    constexpr std::uint32_t SIGN_BIT = 1U << (STRIDE_BITS - 1U);

    const std::uint32_t packed =
        ((instruction.sram.mem_stride1 & STRIDE1_MASK) << STRIDE2_BITS) |
        (instruction.mem.stride2 & STRIDE2_MASK);

    std::int32_t result = static_cast<std::int32_t>(packed);
    if ((packed & SIGN_BIT) != 0U) {
        result -= static_cast<std::int32_t>(1U << STRIDE_BITS);
    }
    return static_cast<int>(result);
}

struct FenceInstruction {
    InstTypes instType = InstTypes::FENCE;
    bool matrix = false;
    bool dma = false;
    bool stop = false;
};

}  // namespace fsa

#endif // !INSTRUCTION_HPP
