#ifndef SPLIT_D_STAGE4_BASELINE_HPP
#define SPLIT_D_STAGE4_BASELINE_HPP

#include <cstdint>

// 占位仅用于独立旧源码捕获；候选验收必须使用非空冻结基线。
struct Stage4BitCase{
    const char* name;
    unsigned length;
    bool causal;
    unsigned input_offset;
    unsigned output_offset;
};
static const Stage4BitCase STAGE4_BIT_CASES[] = {{"", 0U, false, 0U, 0U}};
static const unsigned STAGE4_BIT_CASE_COUNT = 0U;
static const std::uint64_t STAGE4_BIT_INPUTS[] = {0ULL};
static const std::uint64_t STAGE4_BIT_WORDS[] = {0ULL};

#endif
