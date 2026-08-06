#include <cassert>
#include <cmath>

#include "fsa/pe.hpp"

namespace{

/**
 * @brief 在当前float占位模型下比较两个数是否近似相等
 *
 * @param actual PE实际得到的数值
 * @param expected testbench计算的预期数值
 * @return true 两个数的误差足够小
 * @return false 两个数不相等
 */
bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual-expected)<(fsa::acc_t)1.0e-6F;
}

}  // namespace

int main(){
    fsa::PEState current{};
    fsa::PEState next{};
    fsa::PEIO io{};

    fsa::reset_pe_state(current);

    fsa::PECtrl load_ctrl{};
    load_ctrl.load_reg_li = true;

    io.in_ctrl = fsa::make_valid(load_ctrl);
    io.l_input = fsa::make_valid((fsa::elem_t)2.0F);

    fsa::pe_step(current, next, io);

    assert(almostEqual(current.reg, (fsa::elem_t)0.0F));
    assert(almostEqual(next.reg, (fsa::elem_t)2.0F));

    // 模拟时钟上升沿：第1拍算出的next成为第2拍的current。
    current = next;

    /*
     * 第2拍：计算reg*l_input+d_input，即：
     *
     *   2*3+4=10
     *
     * acc_ui=false表示MAC的累加输入in_c选择d_input；
     * mac=true表示MAC结果从u_output向上输出。
     */
    io = fsa::PEIO{};

    fsa::PECtrl mac_ctrl{};
    mac_ctrl.mac = true;
    mac_ctrl.acc_ui = false;

    io.in_ctrl = fsa::make_valid(mac_ctrl);
    io.l_input = fsa::make_valid((fsa::elem_t)3.0F);
    io.d_input = fsa::make_valid((fsa::acc_t)4.0F);

    fsa::pe_step(current, next, io);

    assert(io.u_output.valid);
    assert(almostEqual(io.u_output.bits, (fsa::acc_t)10.0F));

    // 当前控制没有要求更新reg，所以reg应继续保存第1拍装入的2。
    assert(almostEqual(next.reg, (fsa::elem_t)2.0F));

    return 0;
}
