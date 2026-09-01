#include <cassert>
#include <cmath>

#include "fsa/arithmetic.hpp"
#include "fsa/cmp.hpp"

namespace{

bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual-expected)<(fsa::acc_t)1.0e-6F;
}

fsa::ValidData<fsa::CmpControl> makeCmpControl(const fsa::CmpControlCmd cmd){
    fsa::CmpControl ctrl{};
    ctrl.cmd = cmd;
    ctrl.causalCounter = 0;
    return fsa::make_valid(ctrl);
}

}  // namespace

int main(){
    fsa::CMPState current{};
    fsa::CMPState next{};
    fsa::CMPIO io{};

    fsa::reset_cmp_state(current);

    assert(std::isinf(current.oldMax) && current.oldMax<0);
    assert(std::isinf(current.newMax) && current.newMax<0);

    io.in_ctrl = makeCmpControl(fsa::CmpControlCmd::UPDATE);
    io.d_input = fsa::make_valid((fsa::acc_t)3.0F);

    fsa::cmp_step(current, next, io);

    assert(io.d_output.valid);
    // UPDATE沿竖直acc_t载体传输的是FP16位模式，不是FP32数值转换。
    assert(almostEqual(
        (fsa::acc_t)fsa::viewAasE(io.d_output.bits),
        (fsa::acc_t)3.0F
    ));
    assert(almostEqual(next.newMax, (fsa::acc_t)3.0F));

    current = next;

    io = fsa::CMPIO{};
    io.in_ctrl = makeCmpControl(fsa::CmpControlCmd::UPDATE);
    io.d_input = fsa::make_valid((fsa::acc_t)5.0F);

    fsa::cmp_step(current, next, io);

    assert(almostEqual(next.newMax, (fsa::acc_t)5.0F));

    current = next;


    io = fsa::CMPIO{};
    io.in_ctrl = makeCmpControl(fsa::CmpControlCmd::UPDATE);
    io.d_input = fsa::make_valid((fsa::acc_t)2.0F);

    fsa::cmp_step(current, next, io);

    assert(almostEqual(next.newMax, (fsa::acc_t)5.0F));

    current = next;

    io = fsa::CMPIO{};
    io.in_ctrl = makeCmpControl(fsa::CmpControlCmd::RESET);

    fsa::cmp_step(current, next, io);

    assert(!io.d_output.valid);
    assert(std::isinf(next.oldMax) && next.oldMax<0);
    assert(std::isinf(next.newMax) && next.newMax<0);

    return 0;
}
