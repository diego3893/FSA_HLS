/**
 * @file test_accumulator.cpp
 * @brief Accumulator 已实现的单拍命令测试。
 *
 * 当前覆盖 RESET、valid=false、SET_SCALE、EXP_S1、EXP_S2、ACC、ACC_SA
 * 以及每拍两商位的FP32恢复除法RECIPROCAL。
 */

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"

namespace{

bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    const fsa::acc_t tolerance = (fsa::acc_t)1.0e-5F;
    return std::fabs(actual-expected)<tolerance;
}

bool exp2AlmostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    const fsa::acc_t relative_error =
        std::fabs(actual-expected)/std::fabs(expected);
    return relative_error <= (fsa::acc_t)1.0e-3F;
}

bool reciprocalEqual(const fsa::acc_t actual, const fsa::acc_t expected){
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

    const fsa::acc_t relative_error =
        std::fabs((actual-expected)/expected);
    return relative_error <= (fsa::acc_t)1.0e-6F;
}

fsa::ValidData<fsa::AccumulatorControl> makeAccumulatorControl(
        const fsa::AccumulatorCmd cmd){
    fsa::AccumulatorControl ctrl{};
    ctrl.cmd = cmd;
    return fsa::make_valid(ctrl);
}

void assertScaleEquals(const fsa::AccumulatorState& state,
                       const fsa::acc_t expected[fsa::SA_COLS]){
    for(int col=0; col<fsa::SA_COLS; ++col){
        assert(almostEqual(
            state.scale[col],
            expected[(std::size_t)col]
        ));
    }
}

/**
 * @brief 用一拍请求启动四列reciprocal，并推进到固定第15拍完成
 */
void runReciprocalCase(
        fsa::AccumulatorState& current,
        fsa::AccumulatorState& next,
        fsa::AccumulatorIO& io,
        const fsa::acc_t input[fsa::SA_COLS],
        const fsa::acc_t expected[fsa::SA_COLS]){
    // 先把待求倒数的L值装入scale。
    io = fsa::AccumulatorIO{};
    io.ctrl_in = makeAccumulatorControl(fsa::AccumulatorCmd::SET_SCALE);
    for(int col=0; col<fsa::SA_COLS; ++col){
        io.sram_in[(std::size_t)col] = input[(std::size_t)col];
    }
    fsa::accumulator_step(current, next, io);
    current = next;

    // 第1拍：只发送一个周期的RECIPROCAL启动脉冲。
    io = fsa::AccumulatorIO{};
    io.ctrl_in = makeAccumulatorControl(fsa::AccumulatorCmd::RECIPROCAL);
    fsa::accumulator_step(current, next, io);
    for(int col=0; col<fsa::SA_COLS; ++col){
        assert(next.reciprocal[col].phase==fsa::ReciprocalPhase::ITER);
        assert(reciprocalEqual(next.scale[col], input[(std::size_t)col]));
    }
    current = next;

    // 后续不再保持ctrl.valid，并故意改变外部输入。
    // 正确实现必须继续使用启动拍已经拆包保存的scale，而不能重新读这些值。
    io = fsa::AccumulatorIO{};
    for(int col=0; col<fsa::SA_COLS; ++col){
        io.sram_in[(std::size_t)col] = (fsa::acc_t)(1000+col);
        io.sa_in[(std::size_t)col] = (fsa::acc_t)(-1000-col);
    }

    // 第2至14拍：13个ITER周期，每拍组合产生两个商位。
    for(int iter=0; iter<fsa::reciprocalIterationCycles; ++iter){
        fsa::accumulator_step(current, next, io);
        for(int col=0; col<fsa::SA_COLS; ++col){
            assert(reciprocalEqual(
                next.scale[col],
                input[(std::size_t)col]
            ));

            const fsa::ReciprocalPhase expected_phase =
                iter==fsa::reciprocalIterationCycles-1
                    ? fsa::ReciprocalPhase::DONE
                    : fsa::ReciprocalPhase::ITER;
            assert(next.reciprocal[col].phase==expected_phase);
        }
        current = next;
    }

    // 第15拍：规格化、RNE舍入、输出有效并自动写回scale。
    fsa::accumulator_step(current, next, io);
    for(int col=0; col<fsa::SA_COLS; ++col){
        assert(next.reciprocal[col].phase==fsa::ReciprocalPhase::IDLE);
        assert(reciprocalEqual(
            next.scale[col],
            expected[(std::size_t)col]
        ));
        assert(reciprocalEqual(
            io.sram_out[(std::size_t)col],
            expected[(std::size_t)col]
        ));
    }
    current = next;
}

}  // namespace

int main(){
    fsa::AccumulatorState current{};
    fsa::AccumulatorState next{};
    fsa::AccumulatorIO io{};

    // ------------------------------------------------------------------
    // 测试1：复位清空所有跨拍状态。
    // ------------------------------------------------------------------
    for(int col=0; col<fsa::SA_COLS; ++col){
        current.scale[col] = (fsa::acc_t)(col+1);
        current.reciprocal[col].phase = fsa::ReciprocalPhase::ITER;
        current.reciprocal[col].remainder = 123;
        current.reciprocal[col].divisor = 456;
        current.reciprocal[col].quotient = 7;
        current.reciprocal[col].iter_count = 3;
        current.reciprocal[col].result = (fsa::acc_t)(col+10);
    }

    fsa::reset_accumulator_state(current);

    for(int col=0; col<fsa::SA_COLS; ++col){
        assert(almostEqual(current.scale[col], fsa::accZero()));
        assert(current.reciprocal[col].phase==fsa::ReciprocalPhase::IDLE);
        assert(current.reciprocal[col].remainder==0);
        assert(current.reciprocal[col].divisor==0);
        assert(current.reciprocal[col].quotient==0);
        assert(current.reciprocal[col].iter_count==0);
        assert(almostEqual(current.reciprocal[col].result, fsa::accZero()));
    }

    // ------------------------------------------------------------------
    // 测试2：valid=false 时，即使 bits 默认为 SET_SCALE，也不能更新状态。
    // ------------------------------------------------------------------
    const fsa::acc_t zero_scale[fsa::SA_COLS] = {};
    io = fsa::AccumulatorIO{};
    for(int col=0; col<fsa::SA_COLS; ++col){
        io.sram_in[(std::size_t)col] = (fsa::acc_t)(100+col);
    }

    fsa::accumulator_step(current, next, io);
    assertScaleEquals(next, zero_scale);

    // ------------------------------------------------------------------
    // 测试3：SET_SCALE 将每列 SRAM 输入保存到 scale 寄存器。
    // ------------------------------------------------------------------
    const fsa::acc_t initial_scale[fsa::SA_COLS] = {
        (fsa::acc_t)1.0F,
        (fsa::acc_t)2.0F,
        (fsa::acc_t)3.0F,
        (fsa::acc_t)4.0F
    };

    io = fsa::AccumulatorIO{};
    io.ctrl_in = makeAccumulatorControl(fsa::AccumulatorCmd::SET_SCALE);
    for(int col=0; col<fsa::SA_COLS; ++col){
        io.sram_in[(std::size_t)col] = initial_scale[(std::size_t)col];
    }

    fsa::accumulator_step(current, next, io);
    assertScaleEquals(next, initial_scale);
    current = next;

    // ------------------------------------------------------------------
    // 测试4：ACC 计算 sram_out = scale * sram_in，不更新 scale。
    // ------------------------------------------------------------------
    io = fsa::AccumulatorIO{};
    io.ctrl_in = makeAccumulatorControl(fsa::AccumulatorCmd::ACC);
    for(int col=0; col<fsa::SA_COLS; ++col){
        io.sram_in[(std::size_t)col] = (fsa::acc_t)(col+2);
        io.sa_in[(std::size_t)col] = (fsa::acc_t)100.0F;
    }

    fsa::accumulator_step(current, next, io);
    for(int col=0; col<fsa::SA_COLS; ++col){
        const fsa::acc_t expected =
            initial_scale[(std::size_t)col]*(fsa::acc_t)(col+2);
        assert(almostEqual(io.sram_out[(std::size_t)col], expected));
    }
    assertScaleEquals(next, initial_scale);

    // ------------------------------------------------------------------
    // 测试5：ACC_SA 计算 sram_out = scale * sram_in + sa_in。
    // ------------------------------------------------------------------
    io = fsa::AccumulatorIO{};
    io.ctrl_in = makeAccumulatorControl(fsa::AccumulatorCmd::ACC_SA);
    for(int col=0; col<fsa::SA_COLS; ++col){
        io.sram_in[(std::size_t)col] = (fsa::acc_t)(col+2);
        io.sa_in[(std::size_t)col] = (fsa::acc_t)(10*(col+1));
    }

    fsa::accumulator_step(current, next, io);
    for(int col=0; col<fsa::SA_COLS; ++col){
        const fsa::acc_t expected =
            initial_scale[(std::size_t)col]*(fsa::acc_t)(col+2)
            + (fsa::acc_t)(10*(col+1));
        assert(almostEqual(io.sram_out[(std::size_t)col], expected));
    }
    assertScaleEquals(next, initial_scale);

    // ------------------------------------------------------------------
    // 测试6：EXP_S1 计算 scale = sa_in * attentionScale。
    // ------------------------------------------------------------------
    io = fsa::AccumulatorIO{};
    io.ctrl_in = makeAccumulatorControl(fsa::AccumulatorCmd::EXP_S1);
    for(int col=0; col<fsa::SA_COLS; ++col){
        // 对应oldMax-newMax，正常数据通路中该值不大于0。
        io.sa_in[(std::size_t)col] = (fsa::acc_t)(-(col+1));
        // EXP_S1 不应使用普通路径上的 SRAM 输入。
        io.sram_in[(std::size_t)col] = (fsa::acc_t)99.0F;
    }

    fsa::accumulator_step(current, next, io);
    for(int col=0; col<fsa::SA_COLS; ++col){
        const fsa::acc_t expected =
            (fsa::acc_t)(-(col+1))*fsa::attentionScale();
        assert(almostEqual(next.scale[col], expected));
        assert(almostEqual(io.sram_out[(std::size_t)col], expected));
    }

    // EXP_S1的结果成为下一拍EXP_S2读取的current.scale。
    current = next;

    // ------------------------------------------------------------------
    // 测试7：连续执行EXP_S1 -> EXP_S2，得到在线Softmax缩放因子。
    // ------------------------------------------------------------------
    io = fsa::AccumulatorIO{};
    io.ctrl_in = makeAccumulatorControl(fsa::AccumulatorCmd::EXP_S2);

    fsa::accumulator_step(current, next, io);
    for(int col=0; col<fsa::SA_COLS; ++col){
        const fsa::acc_t exp2_input =
            (fsa::acc_t)(-(col+1))*fsa::attentionScale();
        const fsa::acc_t expected =
            (fsa::acc_t)std::exp2((double)exp2_input);

        assert(exp2AlmostEqual(next.scale[col], expected));
        assert(exp2AlmostEqual(io.sram_out[(std::size_t)col], expected));
    }

    current = next;

    // ------------------------------------------------------------------
    // 测试8：恢复除法普通数，包含不能有限表示的1/3。
    // ------------------------------------------------------------------
    static_assert(
        fsa::reciprocalLatency==15,
        "当前测试按1拍启动+13拍迭代+1拍完成检查reciprocal"
    );
    const fsa::acc_t reciprocal_input[fsa::SA_COLS] = {
        (fsa::acc_t)1.0F,
        (fsa::acc_t)2.0F,
        (fsa::acc_t)3.0F,
        (fsa::acc_t)4.0F
    };
    const fsa::acc_t reciprocal_expected[fsa::SA_COLS] = {
        (fsa::acc_t)1.0F,
        (fsa::acc_t)0.5F,
        (fsa::acc_t)(1.0F/3.0F),
        (fsa::acc_t)0.25F
    };
    runReciprocalCase(
        current,
        next,
        io,
        reciprocal_input,
        reciprocal_expected
    );

    // ------------------------------------------------------------------
    // 测试9：最大有限数的倒数落入次正规数，覆盖静态舍入位选择路径。
    // ------------------------------------------------------------------
    const fsa::acc_t maximum =
        std::numeric_limits<fsa::acc_t>::max();
    const fsa::acc_t reciprocal_subnormal_input[fsa::SA_COLS] = {
        maximum,
        -maximum,
        maximum,
        -maximum
    };
    const fsa::acc_t reciprocal_subnormal_expected[fsa::SA_COLS] = {
        (fsa::acc_t)(1.0F/maximum),
        (fsa::acc_t)(-1.0F/maximum),
        (fsa::acc_t)(1.0F/maximum),
        (fsa::acc_t)(-1.0F/maximum)
    };
    runReciprocalCase(
        current,
        next,
        io,
        reciprocal_subnormal_input,
        reciprocal_subnormal_expected
    );

    // ------------------------------------------------------------------
    // 测试10：zero、Infinity、负数与负零的固定延迟特殊值处理。
    // ------------------------------------------------------------------
    const fsa::acc_t infinity =
        std::numeric_limits<fsa::acc_t>::infinity();
    const fsa::acc_t reciprocal_special_input[fsa::SA_COLS] = {
        (fsa::acc_t)0.0F,
        infinity,
        (fsa::acc_t)-4.0F,
        (fsa::acc_t)-0.0F
    };
    const fsa::acc_t reciprocal_special_expected[fsa::SA_COLS] = {
        infinity,
        (fsa::acc_t)0.0F,
        (fsa::acc_t)-0.25F,
        -infinity
    };
    runReciprocalCase(
        current,
        next,
        io,
        reciprocal_special_input,
        reciprocal_special_expected
    );

    std::cout << "[PASS] test_accumulator: reset, invalid, SET_SCALE, "
                 "ACC, ACC_SA, EXP_S1, EXP_S2, RECIPROCAL" << std::endl;
    return 0;
}
