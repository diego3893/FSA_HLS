/**
 * @file test_pe_array_2x2.cpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 使用4个pe_step搭建2x2 PE阵列，模拟Q装载与Q乘K转置
 * @date 2026-08-06
 *
 * 本测试只使用PE，不包含CMP和完整SystolicArray模块。测试过程分为：
 *
 * 1. 参考FSA的LOAD_STATIONARY，把Q按最后一行到第一行的顺序送入阵列；
 * 2. 手动模拟InputDelayer对K输入产生的阶梯延迟；
 * 3. 让部分和从底行向顶行传播，计算Q*K^T；
 * 4. 把顶部输出保存到普通C++数组result并打印。
 *
 * 本文件是软件testbench，不参与HLS综合。
 */

#include <cassert>
#include <cmath>
#include <iostream>

#include "fsa/arithmetic.hpp"
#include "fsa/pe.hpp"

namespace{

constexpr int TEST_ROWS = 2;
constexpr int TEST_COLS = 2;

/**
 * @brief 2x2 PE阵列在相邻step之间保存的状态
 *
 * 这里只保留本测试需要的控制、横向数据和向上数据Pipe。
 */
struct PEArray2x2State{
    fsa::PEState mesh[TEST_ROWS][TEST_COLS]{};
    fsa::ValidData<fsa::PECtrl> ctrl_pipe[TEST_ROWS][TEST_COLS]{};
    fsa::ValidData<fsa::elem_t> r_output_pipe[TEST_ROWS][TEST_COLS]{};
    fsa::ValidData<fsa::acc_t> u_output_pipe[TEST_ROWS][TEST_COLS]{};
};

/**
 * @brief 手动模拟2路InputDelayer所需的一个延迟寄存器
 *
 * FSA计算QK时设置rev_input=true、delay_output=true、rev_output=true。
 * 对2个元素[x0,x1]，最终输出为[上一拍的x0, 本拍的x1]。
 */
struct ManualInputDelayer2State{
    fsa::elem_t delayed_x0{};
};

bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual-expected)<(fsa::acc_t)1.0e-6F;
}

void resetPEArray2x2(PEArray2x2State& state){
    for(int row=0; row<TEST_ROWS; ++row){
        for(int col=0; col<TEST_COLS; ++col){
            fsa::reset_pe_state(state.mesh[row][col]);
            state.ctrl_pipe[row][col] = fsa::make_invalid<fsa::PECtrl>();
            state.r_output_pipe[row][col] = fsa::make_invalid<fsa::elem_t>();
            state.u_output_pipe[row][col] = fsa::make_invalid<fsa::acc_t>();
        }
    }
}

/**
 * @brief 计算2x2 PE阵列的一拍
 *
 * @param current 本拍开始状态
 * @param next 下一拍状态
 * @param left_data 从阵列左侧进入两行的数据
 * @param left_ctrl 从阵列左侧进入两行的控制
 */
void peArray2x2Step(
    const PEArray2x2State& current,
    PEArray2x2State& next,
    const fsa::elem_t left_data[TEST_ROWS],
    const fsa::ValidData<fsa::PECtrl> left_ctrl[TEST_ROWS]){
    next = current;

    for(int row=0; row<TEST_ROWS; ++row){
        for(int col=0; col<TEST_COLS; ++col){
            fsa::PEIO pe_io{};

            // 控制和elem_t数据都从左向右传播，每经过一个PE延迟一拍。
            pe_io.in_ctrl = (col==0) ? left_ctrl[row]
                                     : current.ctrl_pipe[row][col-1];
            pe_io.l_input = (col==0) ? fsa::make_valid(left_data[row])
                                     : current.r_output_pipe[row][col-1];

            // 本测试只使用从下向上的累加方向。
            // 最底行从0开始累加，顶行读取底行上一拍的部分和。
            pe_io.d_input = (row==TEST_ROWS-1)
                ? fsa::make_valid(fsa::accZero())
                : current.u_output_pipe[row+1][col];
            pe_io.u_input = fsa::make_invalid<fsa::acc_t>();

            fsa::pe_step(
                current.mesh[row][col],
                next.mesh[row][col],
                pe_io);

            next.ctrl_pipe[row][col] = pe_io.out_ctrl;
            next.r_output_pipe[row][col] = pe_io.r_output;
            next.u_output_pipe[row][col] = pe_io.u_output;
        }
    }
}

/**
 * @brief 模拟FSA计算QK时2路InputDelayer的一拍
 *
 * @param current 本拍延迟寄存器
 * @param next 下一拍延迟寄存器
 * @param input 本拍输入向量[x0,x1]
 * @param output 错拍后的输出[上一拍x0, 本拍x1]
 */
void manualInputDelayer2Step(
    const ManualInputDelayer2State& current,
    ManualInputDelayer2State& next,
    const fsa::elem_t input[TEST_ROWS],
    fsa::elem_t output[TEST_ROWS]){
    output[0] = current.delayed_x0;
    output[1] = input[1];
    next.delayed_x0 = input[0];
}

void printMatrix(
    const char* name,
    const fsa::acc_t matrix[TEST_ROWS][TEST_COLS]){
    std::cout << name << " =\n";
    for(int row=0; row<TEST_ROWS; ++row){
        std::cout << "  ";
        for(int col=0; col<TEST_COLS; ++col){
            std::cout << matrix[row][col];
            if(col+1<TEST_COLS){
                std::cout << ' ';
            }
        }
        std::cout << '\n';
    }
}

}  // namespace

int main(){
    /*
     * Q的每一行表示一个query，每一列表示一个feature：
     *
     * Q = [1 2]
     *     [3 4]
     */
    const fsa::elem_t q[TEST_COLS][TEST_ROWS] = {
        {(fsa::elem_t)1.0F, (fsa::elem_t)2.0F},
        {(fsa::elem_t)3.0F, (fsa::elem_t)4.0F}
    };

    /*
     * K的每一行表示一个key，每一列表示一个feature：
     *
     * K = [5 6]
     *     [7 8]
     *
     * AttentionScore阶段计算Q*K^T。
     */
    const fsa::elem_t k[TEST_COLS][TEST_ROWS] = {
        {(fsa::elem_t)5.0F, (fsa::elem_t)6.0F},
        {(fsa::elem_t)7.0F, (fsa::elem_t)8.0F}
    };

    const fsa::acc_t expected[TEST_ROWS][TEST_COLS] = {
        {(fsa::acc_t)17.0F, (fsa::acc_t)23.0F},
        {(fsa::acc_t)39.0F, (fsa::acc_t)53.0F}
    };

    fsa::acc_t result[TEST_ROWS][TEST_COLS]{};

    PEArray2x2State current{};
    PEArray2x2State next{};
    resetPEArray2x2(current);

    /*
     * 阶段1：用两拍把Q送入阵列。
     *
     * FSA软件先把Q在query维度反转，所以输入顺序为Q[1]、Q[0]。
     * load_reg_li还会把PE原来的reg向右送，使最终阵列保存Q^T：
     *
     * PE.reg = [1 3]
     *          [2 4]
     */
    fsa::PECtrl load_ctrl_bits{};
    load_ctrl_bits.load_reg_li = true;

    for(int load_cycle=0; load_cycle<TEST_COLS; ++load_cycle){
        const int q_row = TEST_COLS-1-load_cycle;
        fsa::elem_t left_data[TEST_ROWS] = {
            q[q_row][0], q[q_row][1]
        };
        fsa::ValidData<fsa::PECtrl> left_ctrl[TEST_ROWS] = {
            fsa::make_valid(load_ctrl_bits),
            fsa::make_valid(load_ctrl_bits)
        };

        peArray2x2Step(current, next, left_data, left_ctrl);
        current = next;
    }

    // 两拍输入结束后，再空走一拍，让最后一条load控制和Q[1]到达第1列。
    {
        const fsa::elem_t left_data[TEST_ROWS]{};
        const fsa::ValidData<fsa::PECtrl> left_ctrl[TEST_ROWS] = {
            fsa::make_invalid<fsa::PECtrl>(),
            fsa::make_invalid<fsa::PECtrl>()
        };
        peArray2x2Step(current, next, left_data, left_ctrl);
        current = next;
    }

    assert(almostEqual(current.mesh[0][0].reg, q[0][0]));
    assert(almostEqual(current.mesh[1][0].reg, q[0][1]));
    assert(almostEqual(current.mesh[0][1].reg, q[1][0]));
    assert(almostEqual(current.mesh[1][1].reg, q[1][1]));

    /*
     * 阶段2：连续两拍输入K的两行，再用两拍排空阵列。
     *
     * InputDelayer让row1先看到当前key的feature1，row0下一拍再看到同一个
     * key的feature0。MAC控制也从底行向顶行错开一拍。
     */
    ManualInputDelayer2State delayer_current{};
    ManualInputDelayer2State delayer_next{};

    fsa::PECtrl mac_ctrl_bits{};
    mac_ctrl_bits.mac = true;
    mac_ctrl_bits.acc_ui = false;
    mac_ctrl_bits.flow_lr = true;

    for(int compute_cycle=0; compute_cycle<4; ++compute_cycle){
        fsa::elem_t raw_k[TEST_ROWS]{};
        if(compute_cycle<TEST_COLS){
            raw_k[0] = k[compute_cycle][0];
            raw_k[1] = k[compute_cycle][1];
        }

        fsa::elem_t delayed_k[TEST_ROWS]{};
        manualInputDelayer2Step(
            delayer_current,
            delayer_next,
            raw_k,
            delayed_k);
        delayer_current = delayer_next;

        /*
         * flow_up控制：
         *   row1在cycle 0、1有效；
         *   row0在cycle 1、2有效。
         */
        const bool row0_valid = compute_cycle>=1 && compute_cycle<=2;
        const bool row1_valid = compute_cycle>=0 && compute_cycle<=1;
        const fsa::ValidData<fsa::PECtrl> left_ctrl[TEST_ROWS] = {
            fsa::ValidData<fsa::PECtrl>{row0_valid, mac_ctrl_bits},
            fsa::ValidData<fsa::PECtrl>{row1_valid, mac_ctrl_bits}
        };

        peArray2x2Step(current, next, delayed_k, left_ctrl);
        current = next;

        /*
         * 顶行u_output_pipe中的有效数据就是完整点积。
         * 对第col列，当前结果对应的key编号为cycle-1-col。
         */
        for(int col=0; col<TEST_COLS; ++col){
            if(current.u_output_pipe[0][col].valid){
                const int key = compute_cycle-1-col;
                if(key>=0 && key<TEST_COLS){
                    result[col][key] = current.u_output_pipe[0][col].bits;
                }
            }
        }
    }

    for(int row=0; row<TEST_ROWS; ++row){
        for(int col=0; col<TEST_COLS; ++col){
            assert(almostEqual(result[row][col], expected[row][col]));
        }
    }

    const fsa::acc_t q_for_print[TEST_ROWS][TEST_COLS] = {
        {(fsa::acc_t)q[0][0], (fsa::acc_t)q[0][1]},
        {(fsa::acc_t)q[1][0], (fsa::acc_t)q[1][1]}
    };
    const fsa::acc_t k_for_print[TEST_ROWS][TEST_COLS] = {
        {(fsa::acc_t)k[0][0], (fsa::acc_t)k[0][1]},
        {(fsa::acc_t)k[1][0], (fsa::acc_t)k[1][1]}
    };

    printMatrix("Q", q_for_print);
    printMatrix("K", k_for_print);
    printMatrix("Q * K^T", result);

    return 0;
}
