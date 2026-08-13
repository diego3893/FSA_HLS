/**
 * @file test_accumulator_top.cpp
 * @brief Accumulator HLS 顶层的跨事务状态与功能测试。
 *
 * 本测试只通过 accumulator_top 的正式端口观察行为，不直接访问顶层内部
 * static AccumulatorState。一次 accumulator_top 调用表示一个逻辑 step，
 * 但不保证等于综合后的一个物理时钟周期。
 */

#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

#include "fsa/hls/accumulator_top.hpp"

namespace{

int failure_count = 0;

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
void resetTop(fsa::AccumulatorTopOutput& output){
    fsa::AccumulatorTopInput input{};
    input.reset = true;
    accumulator_top(input, output);

    const fsa::AccVector zero = {{
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)0.0F
    }};
    expectVector("RESET output", output.sram_out, zero);
}

/**
 * @brief 用SET_SCALE把四列值装入顶层内部static scale。
 *
 * SET_SCALE当拍的sram_out仍是旧scale参与普通运算得到的候选值，所以这里
 * 不检查本拍输出；下一次ACC才用于间接观察刚写入的scale。
 */
void setScale(
        const fsa::AccVector& scale,
        fsa::AccumulatorTopOutput& output){
    fsa::AccumulatorTopInput input =
        makeCommandInput(fsa::AccumulatorCmd::SET_SCALE);
    input.sram_in = scale;
    accumulator_top(input, output);
}

/**
 * @brief 执行一次ACC，并返回scale*sram_in。
 */
void runAcc(
        const fsa::AccVector& sram_in,
        fsa::AccumulatorTopOutput& output){
    fsa::AccumulatorTopInput input =
        makeCommandInput(fsa::AccumulatorCmd::ACC);
    input.sram_in = sram_in;
    accumulator_top(input, output);
}

/**
 * @brief 检查固定15个逻辑step的reciprocal顶层行为。
 */
void runReciprocalCase(
        const char* stage,
        const fsa::AccVector& denominator,
        const fsa::AccVector& expected,
        const bool check_pending_output,
        fsa::AccumulatorTopOutput& output){
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

    // 第1个逻辑step：只发送一次RECIPROCAL启动脉冲，IDLE->ITER。
    fsa::AccumulatorTopInput input =
        makeCommandInput(fsa::AccumulatorCmd::RECIPROCAL);
    accumulator_top(input, output);

    /**
     * 第2至14个逻辑step：共13次ITER。
     *
     * 后续ctrl.valid保持false，只把sram_in设为1。普通候选数据仍是
     * scale*1，因此可检查倒数没有提前完成；更重要的是，这能证明一次
     * RECIPROCAL启动脉冲已经足够，DONE结果不依赖valid持续拉高。
     */
    input = fsa::AccumulatorTopInput{};
    input.sram_in = one;
    for(int iter=0; iter<fsa::reciprocalIterationCycles; ++iter){
        accumulator_top(input, output);
        if(check_pending_output){
            expectVector(
                "RECIPROCAL pending",
                output.sram_out,
                denominator
            );
        }
    }

    // 第15个逻辑step：DONE产生结果，并自动写回内部scale。
    accumulator_top(input, output);
    expectVector(
        stage,
        output.sram_out,
        expected,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)1.0e-6F
    );

    // 再执行ACC*1，证明刚才的reciprocal结果确实跨调用保存在scale中。
    runAcc(one, output);
    expectVector(
        "RECIPROCAL stored scale",
        output.sram_out,
        expected,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)1.0e-6F
    );
}

}  // namespace

int main(){
    static_assert(
        fsa::SA_COLS==4,
        "当前顶层testbench中的测试向量按4列编写"
    );

    fsa::AccumulatorTopOutput output{};

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
    accumulator_top(input, output);

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
    accumulator_top(input, output);

    const fsa::AccVector acc_sa_expected = {{
        (fsa::acc_t)12.0F,
        (fsa::acc_t)26.0F,
        (fsa::acc_t)42.0F,
        (fsa::acc_t)60.0F
    }};
    expectVector("ACC_SA", output.sram_out, acc_sa_expected);

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
    accumulator_top(input, output);
    expectVector("EXP_S1", output.sram_out, exp_s1_expected);

    input = makeCommandInput(fsa::AccumulatorCmd::EXP_S2);
    accumulator_top(input, output);
    expectVector(
        "EXP_S2",
        output.sram_out,
        exp_s2_expected,
        (fsa::acc_t)1.0e-6F,
        (fsa::acc_t)1.0e-3F
    );

    runAcc(one, output);
    expectVector(
        "EXP_S2 stored scale",
        output.sram_out,
        exp_s2_expected,
        (fsa::acc_t)1.0e-6F,
        (fsa::acc_t)1.0e-3F
    );

    // ------------------------------------------------------------------
    // 测试7：普通有限数reciprocal及15个逻辑step时序。
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
        true,
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
        false,
        output
    );

    // ------------------------------------------------------------------
    // 测试9：reciprocal运行中复位必须取消多周期状态。
    // ------------------------------------------------------------------
    const fsa::AccVector reset_denominator = {{
        (fsa::acc_t)5.0F,
        (fsa::acc_t)6.0F,
        (fsa::acc_t)7.0F,
        (fsa::acc_t)8.0F
    }};
    setScale(reset_denominator, output);
    input = makeCommandInput(fsa::AccumulatorCmd::RECIPROCAL);
    accumulator_top(input, output);

    input = makeCommandInput(fsa::AccumulatorCmd::ACC);
    input.sram_in = one;
    for(int iter=0; iter<3; ++iter){
        accumulator_top(input, output);
    }

    resetTop(output);
    for(int step=0; step<fsa::reciprocalLatency; ++step){
        runAcc(one, output);
        expectVector("RESET cancels RECIPROCAL", output.sram_out, zero);
    }

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
