/**
 * @file test_cmp_top.cpp
 * @brief 单个 CMP HLS 顶层的基础功能测试。
 *
 * 本测试检查：
 * 1. 顶层复位；
 * 2. UPDATE 更新 newMax，并输出 elem_t 的按位视图；
 * 3. PROP_MAX 输出 -newMax；
 * 4. causalCounter 对输入的屏蔽和向右递减；
 * 5. PROP_ZERO 和 CMP 的 RESET 命令。
 *
 * PROP_EXP2_INTERCEPTS 暂不测试，因为当前 exp2_counter 的回绕写法需要先修正。
 */
#include <cassert>
#include <cmath>
#include <iostream>

#include "fsa/arithmetic.hpp"
#include "fsa/hls/cmp_top.hpp"

namespace {

/** @brief 比较两个 acc_t 数值是否近似相等。 */
bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual - expected) < (fsa::acc_t)1.0e-6F;
}

/**
 * @brief 生成一拍有效的 CMP 顶层输入。
 * @param cmd 本拍执行的 CMP 命令
 * @param value 从 PE 列回到 CMP 的数据
 * @param causal_counter causal mask 计数器
 */
fsa::CMPTopInput makeInput(const fsa::CmpControlCmd cmd,
                           const fsa::acc_t value,
                           const std::uint8_t causal_counter = 0){
    fsa::CMPTopInput input{};
    input.ctrl.valid = true;
    input.ctrl.bits.cmd = cmd;
    input.ctrl.bits.causalCounter = causal_counter;
    input.d_input.valid = true;
    input.d_input.bits = value;
    return input;
}

}  // namespace

int main(){
    fsa::CMPTopInput input{};
    fsa::CMPTopOutput output{};

    /* 第一次调用：复位 oldMax、newMax 和 exp2_counter。 */
    input.reset = true;
    cmp_top(input, output);

    assert(!output.ctrl.valid);
    assert(!output.d_output.valid);

    /*
     * UPDATE 3：
     *   newMax <- max(-INF, 3) = 3。
     * UPDATE 的输出不是数值转换后的 acc_t，而是 elem_t 位模式放进 acc_t。
     * 因此需要通过 viewAasE() 取回 elem_t 后再比较。
     */
    input = makeInput(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)3.0F);
    cmp_top(input, output);

    assert(output.ctrl.valid);
    assert(output.ctrl.bits.cmd == fsa::CmpControlCmd::UPDATE);
    assert(output.d_output.valid);
    assert(almostEqual((fsa::acc_t)fsa::viewAasE(output.d_output.bits),
                       (fsa::acc_t)3.0F));

    /* 再输入5，使内部newMax更新为5。 */
    input = makeInput(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)5.0F);
    cmp_top(input, output);

    assert(output.d_output.valid);
    assert(almostEqual((fsa::acc_t)fsa::viewAasE(output.d_output.bits),
                       (fsa::acc_t)5.0F));

    /* PROP_MAX 应输出 0-newMax，也就是-5。 */
    input = makeInput(fsa::CmpControlCmd::PROP_MAX, fsa::accZero());
    cmp_top(input, output);

    assert(output.d_output.valid);
    assert(almostEqual(output.d_output.bits, (fsa::acc_t)-5.0F));

    /*
     * causalCounter=1 时，输入9会先被替换成-INF，所以newMax仍然为5；
     * 控制信号传到右侧时，causalCounter应从1减到0。
     */
    input = makeInput(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)9.0F, 1);
    cmp_top(input, output);

    assert(output.ctrl.valid);
    assert(output.ctrl.bits.causalCounter == 0);
    assert(output.d_output.valid);
    assert(std::isinf((fsa::acc_t)fsa::viewAasE(output.d_output.bits)));
    assert((fsa::acc_t)fsa::viewAasE(output.d_output.bits) < fsa::accZero());

    /* 再传播一次最大值，确认被mask的9没有改写newMax。 */
    input = makeInput(fsa::CmpControlCmd::PROP_MAX, fsa::accZero());
    cmp_top(input, output);

    assert(output.d_output.valid);
    assert(almostEqual(output.d_output.bits, (fsa::acc_t)-5.0F));

    /* PROP_ZERO 只输出0，不修改max状态。 */
    input = makeInput(fsa::CmpControlCmd::PROP_ZERO, fsa::accZero());
    cmp_top(input, output);

    assert(output.d_output.valid);
    assert(almostEqual(output.d_output.bits, fsa::accZero()));

    /* CMP的RESET命令清空max状态，并且本拍数据输出无效。 */
    input = makeInput(fsa::CmpControlCmd::RESET, fsa::accZero());
    cmp_top(input, output);

    assert(output.ctrl.valid);
    assert(!output.d_output.valid);

    /* RESET后重新UPDATE 2，再用PROP_MAX验证状态确实重新开始。 */
    input = makeInput(fsa::CmpControlCmd::UPDATE, (fsa::acc_t)2.0F);
    cmp_top(input, output);

    input = makeInput(fsa::CmpControlCmd::PROP_MAX, fsa::accZero());
    cmp_top(input, output);

    assert(output.d_output.valid);
    assert(almostEqual(output.d_output.bits, (fsa::acc_t)-2.0F));

    std::cout << "[PASS] test_cmp_top: basic CMP paths" << std::endl;
    return 0;
}
