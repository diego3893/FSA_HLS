#ifndef SPLIT_D_STAGE12_BASELINE_HPP
#define SPLIT_D_STAGE12_BASELINE_HPP

// Placeholder used only while the unchanged 73110dc DUT captures official outputs.
// Replaced by complete fixed words before any candidate is tested.
struct Stage12BitCase{
    const char* name;
    unsigned length;
    bool causal;
    unsigned offset;
};
static const unsigned STAGE12_BIT_CASE_COUNT = 0;
static const Stage12BitCase STAGE12_BIT_CASES[1] = {{nullptr, 0, false, 0}};
static const std::uint64_t STAGE12_BIT_WORDS[1] = {0};

#endif
