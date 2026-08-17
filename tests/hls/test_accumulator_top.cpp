/**
 * @file test_accumulator_top.cpp
 * @brief Accumulator HLS 顶层的跨事务状态与功能测试。
 *
 * 本测试只通过 accumulator_top 的正式端口观察行为，不直接访问顶层内部
 * static AccumulatorState。普通命令一次调用是一笔事务；MOD后的RECIPROCAL
 * 也只调用一次，但事务内部目标为固定15个物理时钟，拍数由RTL报告验证。
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "fsa/hls/accumulator_top.hpp"

namespace{

int failure_count = 0;

// MOD: 对应拆分后的HLS输出端口；busy/result_valid物理脉冲由RTL wrapper产生。
struct TopOutput{
    fsa::AccVector sram_out{};
    bool sram_write_valid = false;
    bool reciprocal_result = false;
};

void runTop(const fsa::AccumulatorTopInput& input, TopOutput& output){
    accumulator_top(
        input,
        output.sram_out,
        output.sram_write_valid,
        output.reciprocal_result
    );
}

/**
 * @brief 比较FP32结果，同时正确处理NaN、Infinity和带符号零。
 */
bool fpEqual(
        const fsa::acc_t actual,
        const fsa::acc_t expected,
        const fsa::acc_t absolute_tolerance,
        const fsa::acc_t relative_tolerance){
    if(std::isnan(expected)){
        return std::isnan(actual);
    }

    if(std::isinf(expected)){
        return std::isinf(actual)
            && std::signbit(actual)==std::signbit(expected);
    }

    if(expected==(fsa::acc_t)0.0F){
        return actual==(fsa::acc_t)0.0F
            && std::signbit(actual)==std::signbit(expected);
    }

    const fsa::acc_t difference = std::fabs(actual-expected);
    const fsa::acc_t allowed = absolute_tolerance
        + relative_tolerance*std::fabs(expected);
    return difference<=allowed;
}

/**
 * @brief 检查四列向量；失败时保留阶段名、列号和数值。
 */
void expectVector(
        const char* stage,
        const fsa::AccVector& actual,
        const fsa::AccVector& expected,
        const fsa::acc_t absolute_tolerance = (fsa::acc_t)1.0e-5F,
        const fsa::acc_t relative_tolerance = (fsa::acc_t)1.0e-6F){
    for(std::size_t col=0; col<(std::size_t)fsa::SA_COLS; ++col){
        if(!fpEqual(
                actual[col],
                expected[col],
                absolute_tolerance,
                relative_tolerance)){
            std::cerr << "[FAIL] " << stage
                      << ", col=" << col
                      << ", actual=" << actual[col]
                      << ", expected=" << expected[col]
                      << std::endl;
            ++failure_count;
        }
    }
}

std::uint32_t floatBits(const fsa::acc_t value){
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

fsa::acc_t floatFromBits(const std::uint32_t bits){
    fsa::acc_t value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

fsa::AccVector vectorFromBits(const std::uint32_t bits[fsa::SA_COLS]){
    fsa::AccVector value{};
    for(int col=0; col<fsa::SA_COLS; ++col){
        value[(std::size_t)col] = floatFromBits(bits[(std::size_t)col]);
    }
    return value;
}

// MOD: reciprocal要求FP32 RNE按位一致；容差比较会漏掉多ULP错误。
void expectVectorBits(
        const char* stage,
        const fsa::AccVector& actual,
        const fsa::AccVector& expected){
    for(std::size_t col=0; col<(std::size_t)fsa::SA_COLS; ++col){
        if(floatBits(actual[col])!=floatBits(expected[col])){
            std::cerr << "[FAIL] " << stage
                      << ", col=" << col
                      << ", actual_bits=0x" << std::hex << floatBits(actual[col])
                      << ", expected_bits=0x" << floatBits(expected[col])
                      << std::dec << std::endl;
            ++failure_count;
        }
    }
}

/**
 * @brief 构造一拍有效的Accumulator命令。
 */
fsa::AccumulatorTopInput makeCommandInput(const fsa::AccumulatorCmd cmd){
    fsa::AccumulatorTopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.cmd = cmd;
    return input;
}

/**
 * @brief 发起一次显式顶层复位事务，并检查本次输出为零。
 */
void resetTop(TopOutput& output){
    fsa::AccumulatorTopInput input{};
    input.reset = true;
    runTop(input, output);

    const fsa::AccVector zero = {{
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F
    }};
    expectVector("RESET output", output.sram_out, zero);
    if(output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] RESET valid flags" << std::endl;
        ++failure_count;
    }
}

/**
 * @brief 用SET_SCALE把四列值装入顶层内部static scale。
 *
 * SET_SCALE当拍的sram_out仍是旧scale参与普通运算得到的候选值，所以这里
 * 不检查本拍输出；下一次ACC才用于间接观察刚写入的scale。
 */
void setScale(
        const fsa::AccVector& scale,
        TopOutput& output){
    fsa::AccumulatorTopInput input =
        makeCommandInput(fsa::AccumulatorCmd::SET_SCALE);
    input.sram_in = scale;
    runTop(input, output);
    if(output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] SET_SCALE valid flags" << std::endl;
        ++failure_count;
    }
}

/**
 * @brief 执行一次ACC，并返回scale*sram_in。
 */
void runAcc(
        const fsa::AccVector& sram_in,
        TopOutput& output){
    fsa::AccumulatorTopInput input =
        makeCommandInput(fsa::AccumulatorCmd::ACC);
    input.sram_in = sram_in;
    runTop(input, output);
    if(!output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] ACC valid flags" << std::endl;
        ++failure_count;
    }
}

/**
 * @brief 检查单次顶层事务内完成的四列reciprocal行为。
 */
void runReciprocalCase(
        const char* stage,
        const fsa::AccVector& denominator,
        const fsa::AccVector& expected,
        TopOutput& output,
        const bool check_stored_scale = true){
    static_assert(
        fsa::reciprocalLatency==15,
        "测试假定reciprocal为1次启动、13次ITER和1次DONE"
    );

    const fsa::AccVector one = {{
        (fsa::acc_t)1.0F,
        (fsa::acc_t)1.0F,
        (fsa::acc_t)1.0F,
        (fsa::acc_t)1.0F
    }};

    // 预备事务：先把分母装入跨调用保存的scale。
    setScale(denominator, output);

    // MOD: 只发送一笔RECIPROCAL事务；13次ITER由事务内部自行推进。
    fsa::AccumulatorTopInput input =
        makeCommandInput(fsa::AccumulatorCmd::RECIPROCAL);
    runTop(input, output);
    expectVectorBits(stage, output.sram_out, expected);
    if(!output.reciprocal_result || output.sram_write_valid){
        std::cerr << "[FAIL] " << stage
                  << ": reciprocal_result/write_valid protocol" << std::endl;
        ++failure_count;
    }

    if(check_stored_scale){
        // 再执行ACC*1，证明reciprocal结果确实跨调用保存在scale中。
        runAcc(one, output);
        expectVectorBits("RECIPROCAL stored scale", output.sram_out, expected);
    }
}

}  // namespace

int main(){
    static_assert(
        fsa::SA_COLS==4,
        "当前顶层testbench中的测试向量按4列编写"
    );

    TopOutput output{};

    const fsa::AccVector zero = {{
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F
    }};
    const fsa::AccVector one = {{
        (fsa::acc_t)1.0F,
        (fsa::acc_t)1.0F,
        (fsa::acc_t)1.0F,
        (fsa::acc_t)1.0F
    }};

    // ------------------------------------------------------------------
    // 测试1：顶层显式复位。
    // ------------------------------------------------------------------
    resetTop(output);

    // ------------------------------------------------------------------
    // 测试2：SET_SCALE跨顶层调用保存四列独立状态。
    // ------------------------------------------------------------------
    const fsa::AccVector initial_scale = {{
        (fsa::acc_t)1.0F,
        (fsa::acc_t)2.0F,
        (fsa::acc_t)3.0F,
        (fsa::acc_t)4.0F
    }};
    setScale(initial_scale, output);
    runAcc(one, output);
    expectVector("SET_SCALE stored state", output.sram_out, initial_scale);

    // ------------------------------------------------------------------
    // 测试3：ctrl.valid=false不能把默认SET_SCALE命令写入状态。
    // ------------------------------------------------------------------
    fsa::AccumulatorTopInput input{};
    input.sram_in = {{
        (fsa::acc_t)101.0F,
        (fsa::acc_t)102.0F,
        (fsa::acc_t)103.0F,
        (fsa::acc_t)104.0F
    }};
    input.sa_in = {{
        (fsa::acc_t)-101.0F,
        (fsa::acc_t)-102.0F,
        (fsa::acc_t)-103.0F,
        (fsa::acc_t)-104.0F
    }};
    runTop(input, output);
    if(output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] invalid valid flags" << std::endl;
        ++failure_count;
    }

    // invalid拍的sram_out只是候选值，没有有效语义；下一拍ACC*1观察状态。
    runAcc(one, output);
    expectVector("invalid keeps scale", output.sram_out, initial_scale);

    // ------------------------------------------------------------------
    // 测试4：ACC计算scale*sram_in，而且不更新scale。
    // ------------------------------------------------------------------
    const fsa::AccVector acc_sram = {{
        (fsa::acc_t)2.0F,
        (fsa::acc_t)3.0F,
        (fsa::acc_t)4.0F,
        (fsa::acc_t)5.0F
    }};
    const fsa::AccVector acc_expected = {{
        (fsa::acc_t)2.0F,
        (fsa::acc_t)6.0F,
        (fsa::acc_t)12.0F,
        (fsa::acc_t)20.0F
    }};
    runAcc(acc_sram, output);
    expectVector("ACC", output.sram_out, acc_expected);

    // ------------------------------------------------------------------
    // 测试5：ACC_SA计算scale*sram_in+sa_in。
    // ------------------------------------------------------------------
    input = makeCommandInput(fsa::AccumulatorCmd::ACC_SA);
    input.sram_in = acc_sram;
    input.sa_in = {{
        (fsa::acc_t)10.0F,
        (fsa::acc_t)20.0F,
        (fsa::acc_t)30.0F,
        (fsa::acc_t)40.0F
    }};
    runTop(input, output);

    const fsa::AccVector acc_sa_expected = {{
        (fsa::acc_t)12.0F,
        (fsa::acc_t)26.0F,
        (fsa::acc_t)42.0F,
        (fsa::acc_t)60.0F
    }};
    expectVector("ACC_SA", output.sram_out, acc_sa_expected);
    if(!output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] ACC_SA valid flags" << std::endl;
        ++failure_count;
    }

    // MOD: 覆盖普通有限值、溢出、次正规和半ULP ties-to-even边界。
    // Vitis 2024.2 CSIM会把专门区分融合/非融合的抵消向量算成0，
    // 因此该差异不作为CSIM门禁；融合硬件实例需在综合报告中核对。
    const std::uint32_t fma_scale_bits[fsa::SA_COLS] = {
        0x3fc00000U, 0x7f7fffffU, 0x00800000U, 0x00000001U
    };
    const std::uint32_t fma_sram_bits[fsa::SA_COLS] = {
        0x40000000U, 0x40000000U, 0x3f000000U, 0x3f000000U
    };
    const std::uint32_t fma_sa_bits[fsa::SA_COLS] = {
        0xbf800000U, 0x00000000U, 0x00000000U, 0x00000000U
    };
    const std::uint32_t fma_expected_bits[fsa::SA_COLS] = {
        0x40000000U, 0x7f800000U, 0x00400000U, 0x00000000U
    };

    setScale(vectorFromBits(fma_scale_bits), output);
    input = makeCommandInput(fsa::AccumulatorCmd::ACC_SA);
    input.sram_in = vectorFromBits(fma_sram_bits);
    input.sa_in = vectorFromBits(fma_sa_bits);
    runTop(input, output);
    expectVectorBits(
        "ACC_SA FP32 boundary",
        output.sram_out,
        vectorFromBits(fma_expected_bits)
    );
    if(!output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] ACC_SA FP32 boundary valid flags" << std::endl;
        ++failure_count;
    }

    // ------------------------------------------------------------------
    // 测试6：连续EXP_S1 -> EXP_S2，并通过ACC*1检查scale保存结果。
    // ------------------------------------------------------------------
    const fsa::AccVector max_difference = {{
        (fsa::acc_t)-1.0F,
        (fsa::acc_t)-2.0F,
        (fsa::acc_t)-3.0F,
        (fsa::acc_t)-4.0F
    }};
    const fsa::acc_t attention_scale_reference =
        (fsa::acc_t)(std::log2(std::exp(1.0))
            /std::sqrt((double)fsa::SA_ROWS));

    fsa::AccVector exp_s1_expected{};
    fsa::AccVector exp_s2_expected{};
    for(std::size_t col=0; col<(std::size_t)fsa::SA_COLS; ++col){
        exp_s1_expected[col] =
            max_difference[col]*attention_scale_reference;
        exp_s2_expected[col] =
            (fsa::acc_t)std::exp2((double)exp_s1_expected[col]);
    }

    input = makeCommandInput(fsa::AccumulatorCmd::EXP_S1);
    input.sa_in = max_difference;
    input.sram_in = {{
        (fsa::acc_t)99.0F,
        (fsa::acc_t)99.0F,
        (fsa::acc_t)99.0F,
        (fsa::acc_t)99.0F
    }};
    runTop(input, output);
    expectVector("EXP_S1", output.sram_out, exp_s1_expected);
    if(output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] EXP_S1 valid flags" << std::endl;
        ++failure_count;
    }

    input = makeCommandInput(fsa::AccumulatorCmd::EXP_S2);
    runTop(input, output);
    expectVector(
        "EXP_S2",
        output.sram_out,
        exp_s2_expected,
        (fsa::acc_t)1.0e-6F,
        (fsa::acc_t)1.0e-3F
    );
    if(output.sram_write_valid || output.reciprocal_result){
        std::cerr << "[FAIL] EXP_S2 valid flags" << std::endl;
        ++failure_count;
    }

    runAcc(one, output);
    expectVector(
        "EXP_S2 stored scale",
        output.sram_out,
        exp_s2_expected,
        (fsa::acc_t)1.0e-6F,
        (fsa::acc_t)1.0e-3F
    );

    // ------------------------------------------------------------------
    // 测试7：普通有限数reciprocal；目标15物理拍由综合/RTL测试核对。
    // ------------------------------------------------------------------
    const fsa::AccVector reciprocal_input = {{
        (fsa::acc_t)1.0F,
        (fsa::acc_t)2.0F,
        (fsa::acc_t)3.0F,
        (fsa::acc_t)4.0F
    }};
    const fsa::AccVector reciprocal_expected = {{
        (fsa::acc_t)1.0F,
        (fsa::acc_t)0.5F,
        (fsa::acc_t)(1.0F/3.0F),
        (fsa::acc_t)0.25F
    }};
    runReciprocalCase(
        "RECIPROCAL finite",
        reciprocal_input,
        reciprocal_expected,
        output
    );

    // ------------------------------------------------------------------
    // 测试8：zero、Infinity、负数和负零的reciprocal。
    // ------------------------------------------------------------------
    const fsa::acc_t infinity =
        std::numeric_limits<fsa::acc_t>::infinity();
    const fsa::AccVector reciprocal_special_input = {{
        (fsa::acc_t)0.0F,
        infinity,
        (fsa::acc_t)-4.0F,
        (fsa::acc_t)-0.0F
    }};
    const fsa::AccVector reciprocal_special_expected = {{
        infinity,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)-0.25F,
        -infinity
    }};
    runReciprocalCase(
        "RECIPROCAL special",
        reciprocal_special_input,
        reciprocal_special_expected,
        output,
        false
    );

    // ------------------------------------------------------------------
    // 测试9：NaN、次正规、溢出阈值和RNE边界按FP32位模式比较。
    // ------------------------------------------------------------------
    const char* reciprocal_boundary_names[] = {
        "RECIPROCAL zero/Inf bits",
        "RECIPROCAL NaN canonicalization",
        "RECIPROCAL overflow threshold",
        "RECIPROCAL normal/subnormal input",
        "RECIPROCAL RNE",
        "RECIPROCAL output normal/subnormal boundary"
    };
    const std::uint32_t reciprocal_boundary_inputs[][fsa::SA_COLS] = {
        {0x00000000U, 0x80000000U, 0x7f800000U, 0xff800000U},
        {0x7fc12345U, 0x7f800001U, 0xffc12345U, 0xff800001U},
        {0x00000001U, 0x001fffffU, 0x00200000U, 0x00200001U},
        {0x007fffffU, 0x00800000U, 0x00800001U, 0x3f7fffffU},
        {0x3f800001U, 0x3fffffffU, 0x40000001U, 0x41c80000U},
        {0x7e7fffffU, 0x7e800000U, 0x7e800001U, 0x7f7fffffU}
    };
    const std::uint32_t reciprocal_boundary_expected[][fsa::SA_COLS] = {
        {0x7f800000U, 0xff800000U, 0x00000000U, 0x80000000U},
        {0x7fc00000U, 0x7fc00000U, 0x7fc00000U, 0x7fc00000U},
        {0x7f800000U, 0x7f800000U, 0x7f800000U, 0x7f7ffff8U},
        {0x7e800001U, 0x7e800000U, 0x7e7ffffeU, 0x3f800001U},
        {0x3f7ffffeU, 0x3f000001U, 0x3efffffeU, 0x3d23d70aU},
        {0x00800001U, 0x00800000U, 0x007fffffU, 0x00200000U}
    };

    for(std::size_t case_index=0;
            case_index<sizeof(reciprocal_boundary_names)
                /sizeof(reciprocal_boundary_names[0]);
            ++case_index){
        const fsa::AccVector boundary_input =
            vectorFromBits(reciprocal_boundary_inputs[case_index]);
        const fsa::AccVector boundary_expected =
            vectorFromBits(reciprocal_boundary_expected[case_index]);
        runReciprocalCase(
            reciprocal_boundary_names[case_index],
            boundary_input,
            boundary_expected,
            output,
            case_index>1
        );
    }

    // ------------------------------------------------------------------
    // 测试10：事务完成后显式复位必须清除写回的reciprocal scale。
    // ------------------------------------------------------------------
    const fsa::AccVector reset_denominator = {{
        (fsa::acc_t)5.0F,
        (fsa::acc_t)6.0F,
        (fsa::acc_t)7.0F,
        (fsa::acc_t)8.0F
    }};
    setScale(reset_denominator, output);
    input = makeCommandInput(fsa::AccumulatorCmd::RECIPROCAL);
    runTop(input, output);

    resetTop(output);
    runAcc(one, output);
    expectVector("RESET clears RECIPROCAL scale", output.sram_out, zero);

    if(failure_count!=0){
        std::cerr << "[FAIL] test_accumulator_top: "
                  << failure_count << " checks failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_accumulator_top: RESET, invalid, SET_SCALE, "
                 "ACC, ACC_SA, EXP_S1, EXP_S2, RECIPROCAL"
              << std::endl;
    return 0;
}
