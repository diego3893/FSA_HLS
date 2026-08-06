/**
 * @file test_attn_score_2x2_trace.cpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 用固定2x2 testbench逐拍观察FSA计算S=Q*K^T
 *
 * 本文件只属于软件测试，不参与HLS综合。阵列规模固定为2x2，不会修改
 * config.hpp中的正式硬件规模。PE和CMP直接调用工程中的pe_step、cmp_step；
 * 2路Delayer和2x2连线在本文件中按正式模块的连接方式搭建。
 */

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>

#include "fsa/arithmetic.hpp"
#include "fsa/cmp.hpp"
#include "fsa/pe.hpp"

namespace{

constexpr int TEST_ROWS = 2;
constexpr int TEST_COLS = 2;

bool almostEqual(const fsa::acc_t actual, const fsa::acc_t expected){
    return std::fabs(actual-expected)<(fsa::acc_t)1.0e-6F;
}

/**
 * @brief 固定2路Delayer的跨拍状态
 * @tparam T elem_t表示InputDelayer，acc_t表示OutputDelayer
 */
template <typename T>
struct Delayer2State{
    T out_delay_pipe[TEST_ROWS][TEST_ROWS]{};
    bool rev_out_r = false;
    bool delay_r = false;
};

/// @brief 固定2路InputDelayer的端口
struct InputDelayer2IO{
    bool in_valid = false;
    fsa::elem_t in_data[TEST_ROWS]{};
    bool rev_input = false;
    bool delay_output = false;
    bool rev_output = false;
    fsa::elem_t out[TEST_ROWS]{};
};

/// @brief 固定2路OutputDelayer的端口
struct OutputDelayer2IO{
    fsa::acc_t in[TEST_COLS]{};
    fsa::acc_t out[TEST_COLS]{};
};

/**
 * @brief 计算固定2路Delayer的一拍
 *
 * 逻辑与src/core/delayer.cpp相同：先可选反转输入，第i路延迟i拍，
 * 再可选反转输出。这里只把向量长度固定为2，避免修改正式config.hpp。
 */
template <typename T>
void delayer2Step(
    const Delayer2State<T>& current,
    Delayer2State<T>& next,
    const T input[TEST_ROWS],
    const bool input_valid,
    const bool rev_input,
    const bool delay_output,
    const bool rev_output,
    T output[TEST_ROWS]){
    next = current;

    T in_data[TEST_ROWS]{};
    T out_delay[TEST_ROWS]{};
    T selected_output[TEST_ROWS]{};

    for(int lane=0; lane<TEST_ROWS; ++lane){
        const int input_lane = rev_input ? TEST_ROWS-1-lane : lane;
        in_data[lane] = input[input_lane];
    }

    out_delay[0] = in_data[0];
    out_delay[1] = current.out_delay_pipe[1][0];
    next.out_delay_pipe[1][0] = in_data[1];

    const bool delay = input_valid ? delay_output : current.delay_r;
    const bool rev_out = input_valid ? rev_output : current.rev_out_r;
    if(input_valid){
        next.delay_r = delay_output;
        next.rev_out_r = rev_output;
    }

    for(int lane=0; lane<TEST_ROWS; ++lane){
        selected_output[lane] = delay ? out_delay[lane] : in_data[lane];
    }
    for(int lane=0; lane<TEST_ROWS; ++lane){
        const int output_lane = rev_out ? TEST_ROWS-1-lane : lane;
        output[output_lane] = selected_output[lane];
    }
}

void inputDelayer2Step(
    const Delayer2State<fsa::elem_t>& current,
    Delayer2State<fsa::elem_t>& next,
    InputDelayer2IO& io){
    delayer2Step(
        current,
        next,
        io.in_data,
        io.in_valid,
        io.rev_input,
        io.delay_output,
        io.rev_output,
        io.out);
}

void outputDelayer2Step(
    const Delayer2State<fsa::acc_t>& current,
    Delayer2State<fsa::acc_t>& next,
    OutputDelayer2IO& io){
    // 对应OutputDelayer.scala中的固定true/true/true配置。
    delayer2Step(current, next, io.in, true, true, true, true, io.out);
}

/**
 * @brief 固定2x2 SA的全部跨拍状态
 *
 * 数据字段与正式SystolicArrayState一一对应，数组长度固定为2。
 */
struct SystolicArray2State{
    fsa::PEState mesh[TEST_ROWS][TEST_COLS]{};
    fsa::CMPState cmp_array[TEST_COLS]{};
    fsa::ValidData<fsa::CmpControl> cmp_ctrl_pipe[TEST_COLS]{};
    fsa::ValidData<fsa::PECtrl> pe_ctrl_pipe[TEST_ROWS][TEST_COLS]{};
    fsa::ValidData<fsa::acc_t> cmp_d_output_pipe[TEST_COLS]{};
    fsa::ValidData<fsa::elem_t> r_output_pipe[TEST_ROWS][TEST_COLS]{};
    fsa::ValidData<fsa::acc_t> d_output_pipe[TEST_ROWS][TEST_COLS]{};
    fsa::ValidData<fsa::acc_t> u_output_pipe[TEST_ROWS][TEST_COLS]{};
};

/// @brief 固定2x2 SA的外部端口
struct SystolicArray2IO{
    fsa::ValidData<fsa::CmpControl> cmp_ctrl;
    fsa::ValidData<fsa::PECtrl> pe_ctrl[TEST_ROWS]{};
    fsa::elem_t pe_data[TEST_ROWS]{};
    fsa::ValidData<fsa::acc_t> acc_out[TEST_COLS]{};
};

void resetSystolicArray2State(SystolicArray2State& state){
    for(int row=0; row<TEST_ROWS; ++row){
        for(int col=0; col<TEST_COLS; ++col){
            fsa::reset_pe_state(state.mesh[row][col]);
            state.pe_ctrl_pipe[row][col] = fsa::make_invalid<fsa::PECtrl>();
            state.r_output_pipe[row][col] = fsa::make_invalid<fsa::elem_t>();
            state.d_output_pipe[row][col] = fsa::make_invalid<fsa::acc_t>();
            state.u_output_pipe[row][col] = fsa::make_invalid<fsa::acc_t>();
        }
    }
    for(int col=0; col<TEST_COLS; ++col){
        fsa::reset_cmp_state(state.cmp_array[col]);
        state.cmp_ctrl_pipe[col] = fsa::make_invalid<fsa::CmpControl>();
        state.cmp_d_output_pipe[col] = fsa::make_invalid<fsa::acc_t>();
    }
}

/**
 * @brief 用正式PE/CMP step函数计算固定2x2 SA的一拍
 *
 * 连接顺序与src/core/systolic_array.cpp一致，只是循环上界固定为2。
 */
void systolicArray2Step(
    const SystolicArray2State& current,
    SystolicArray2State& next,
    SystolicArray2IO& io){
    next = current;

    for(int col=0; col<TEST_COLS; ++col){
        io.acc_out[col] = current.d_output_pipe[TEST_ROWS-1][col];
    }

    for(int col=0; col<TEST_COLS; ++col){
        fsa::CMPIO cmp_io{};
        cmp_io.in_ctrl = (col==0)
            ? io.cmp_ctrl
            : current.cmp_ctrl_pipe[col-1];
        cmp_io.d_input = current.u_output_pipe[0][col];

        fsa::cmp_step(
            current.cmp_array[col],
            next.cmp_array[col],
            cmp_io);

        next.cmp_ctrl_pipe[col] = cmp_io.out_ctrl;
        next.cmp_d_output_pipe[col] = cmp_io.d_output;
    }

    for(int row=0; row<TEST_ROWS; ++row){
        for(int col=0; col<TEST_COLS; ++col){
            fsa::PEIO pe_io{};
            pe_io.in_ctrl = (col==0)
                ? io.pe_ctrl[row]
                : current.pe_ctrl_pipe[row][col-1];
            pe_io.l_input = (col==0)
                ? fsa::make_valid(io.pe_data[row])
                : current.r_output_pipe[row][col-1];
            pe_io.u_input = (row==0)
                ? current.cmp_d_output_pipe[col]
                : current.d_output_pipe[row-1][col];
            pe_io.d_input = (row==TEST_ROWS-1)
                ? fsa::make_valid(fsa::accZero())
                : current.u_output_pipe[row+1][col];

            fsa::pe_step(
                current.mesh[row][col],
                next.mesh[row][col],
                pe_io);

            next.pe_ctrl_pipe[row][col] = pe_io.out_ctrl;
            next.r_output_pipe[row][col] = pe_io.r_output;
            next.d_output_pipe[row][col] = pe_io.d_output;
            next.u_output_pipe[row][col] = pe_io.u_output;
        }
    }
}

template <typename T>
void printVector(const char* name, const T data[TEST_ROWS]){
    std::cout << "  " << std::left << std::setw(27) << name << "[";
    for(int index=0; index<TEST_ROWS; ++index){
        std::cout << std::right << std::setw(6) << data[index];
        if(index+1<TEST_ROWS){
            std::cout << ",";
        }
    }
    std::cout << "]\n";
}

template <typename T>
void printValidVector(
    const char* name,
    const fsa::ValidData<T> data[TEST_COLS]){
    std::cout << "  " << std::left << std::setw(27) << name << "[";
    for(int col=0; col<TEST_COLS; ++col){
        if(data[col].valid){
            std::cout << std::right << std::setw(6) << data[col].bits;
        }else{
            std::cout << std::right << std::setw(6) << "--";
        }
        if(col+1<TEST_COLS){
            std::cout << ",";
        }
    }
    std::cout << "]\n";
}

void printPERegisters(const SystolicArray2State& state){
    std::cout << "  PE.reg（拍末）\n";
    for(int row=0; row<TEST_ROWS; ++row){
        std::cout << "    row" << row << ": [";
        for(int col=0; col<TEST_COLS; ++col){
            std::cout << std::setw(6) << state.mesh[row][col].reg;
            if(col+1<TEST_COLS){
                std::cout << ",";
            }
        }
        std::cout << "]\n";
    }
}

template <typename T>
void printPEPipe(
    const char* name,
    const fsa::ValidData<T> pipe[TEST_ROWS][TEST_COLS]){
    std::cout << "  " << name << "（拍末，--表示valid=false）\n";
    for(int row=0; row<TEST_ROWS; ++row){
        std::cout << "    row" << row << ": [";
        for(int col=0; col<TEST_COLS; ++col){
            if(pipe[row][col].valid){
                std::cout << std::setw(6) << pipe[row][col].bits;
            }else{
                std::cout << std::setw(6) << "--";
            }
            if(col+1<TEST_COLS){
                std::cout << ",";
            }
        }
        std::cout << "]\n";
    }
}

template <typename T>
void printDelayerPipe(const char* name, const Delayer2State<T>& state){
    std::cout << "  " << name << ".pipe（拍末）\n";
    std::cout << "    lane0: 无寄存器\n";
    std::cout << "    lane1: [" << state.out_delay_pipe[1][0] << "]\n";
}

void printCMPState(const SystolicArray2State& state){
    std::cout << "  CMP内部状态（拍末）\n";
    for(int col=0; col<TEST_COLS; ++col){
        std::cout << "    CMP" << col
                  << ": oldMax=" << std::setw(6) << state.cmp_array[col].oldMax
                  << ", newMax=" << std::setw(6) << state.cmp_array[col].newMax
                  << '\n';
    }
    printValidVector("CMP->PE data pipe", state.cmp_d_output_pipe);
}

void printCycle(
    const int cycle,
    const char* stage,
    const InputDelayer2IO& input_io,
    const Delayer2State<fsa::elem_t>& input_next,
    const SystolicArray2State& sa_current,
    const SystolicArray2State& sa_next,
    const SystolicArray2IO& sa_io,
    const OutputDelayer2IO& output_io,
    const Delayer2State<fsa::acc_t>& output_next){
    std::cout << "\n============================================================\n";
    std::cout << "cycle " << cycle << " : " << stage << '\n';
    std::cout << "  普通数据是本拍端口值；标有‘拍末’的是next状态。\n";

    std::cout << "\n[InputDelayer]\n";
    std::cout << "  input valid                "
              << (input_io.in_valid ? "true" : "false") << '\n';
    printVector("raw input", input_io.in_data);
    printVector("delayed output -> SA", input_io.out);
    printDelayerPipe("InputDelayer", input_next);

    std::cout << "\n[PE array]\n";
    printPERegisters(sa_next);
    printPEPipe("left->right data pipe", sa_next.r_output_pipe);
    printPEPipe("bottom->top data pipe", sa_next.u_output_pipe);
    printPEPipe("top->bottom data pipe", sa_next.d_output_pipe);

    std::cout << "\n[CMP array]\n";
    printValidVector("PE top -> CMP input", sa_current.u_output_pipe[0]);
    printCMPState(sa_next);

    std::cout << "\n[SA bottom and OutputDelayer]\n";
    printValidVector("SA bottom acc_out", sa_io.acc_out);
    printVector("OutputDelayer output", output_io.out);
    printDelayerPipe("OutputDelayer", output_next);
}

}  // namespace

int main(){
    std::cout << std::fixed << std::setprecision(1);

    const fsa::elem_t q[TEST_ROWS][TEST_COLS] = {
        {(fsa::elem_t)1, (fsa::elem_t)2},
        {(fsa::elem_t)3, (fsa::elem_t)4}
    };
    const fsa::elem_t k[TEST_ROWS][TEST_COLS] = {
        {(fsa::elem_t)5, (fsa::elem_t)6},
        {(fsa::elem_t)7, (fsa::elem_t)8}
    };
    const fsa::acc_t expected[TEST_ROWS][TEST_COLS] = {
        {(fsa::acc_t)17, (fsa::acc_t)23},
        {(fsa::acc_t)39, (fsa::acc_t)53}
    };
    fsa::acc_t result[TEST_ROWS][TEST_COLS]{};

    Delayer2State<fsa::elem_t> input_current{};
    Delayer2State<fsa::elem_t> input_next{};
    SystolicArray2State sa_current{};
    SystolicArray2State sa_next{};
    Delayer2State<fsa::acc_t> output_current{};
    Delayer2State<fsa::acc_t> output_next{};
    resetSystolicArray2State(sa_current);

    for(int cycle=0; cycle<=10; ++cycle){
        InputDelayer2IO input_io{};
        SystolicArray2IO sa_io{};
        OutputDelayer2IO output_io{};

        sa_io.cmp_ctrl = fsa::make_invalid<fsa::CmpControl>();
        for(int row=0; row<TEST_ROWS; ++row){
            sa_io.pe_ctrl[row] = fsa::make_invalid<fsa::PECtrl>();
        }

        const char* stage = "排空流水线";

        if(cycle<=1){
            // Q按query方向反序读入；InputDelayer在LOAD_STATIONARY阶段直通。
            const int q_row = 1-cycle;
            input_io.in_data[0] = q[q_row][0];
            input_io.in_data[1] = q[q_row][1];
            input_io.in_valid = true;

            fsa::PECtrl load_q{};
            load_q.load_reg_li = true;
            for(int row=0; row<TEST_ROWS; ++row){
                sa_io.pe_ctrl[row] = fsa::make_valid(load_q);
            }
            stage = "Q经InputDelayer直通并装入PE";
        }else if(cycle==2){
            stage = "排空Q的left->right Pipe";
        }else{
            const int compute_cycle = cycle-3;

            // FSA的ATTN_SCORE配置：输入反转、阶梯延迟、输出反转。
            input_io.rev_input = true;
            input_io.delay_output = true;
            input_io.rev_output = true;
            if(compute_cycle<2){
                input_io.in_data[0] = k[compute_cycle][0];
                input_io.in_data[1] = k[compute_cycle][1];
                input_io.in_valid = true;
            }

            fsa::PECtrl mac{};
            mac.mac = true;
            mac.acc_ui = false;
            mac.flow_lr = true;
            if(compute_cycle>=1 && compute_cycle<=2){
                sa_io.pe_ctrl[0] = fsa::make_valid(mac);
            }
            if(compute_cycle>=0 && compute_cycle<=1){
                sa_io.pe_ctrl[1] = fsa::make_valid(mac);
            }

            if(compute_cycle>=2 && compute_cycle<=3){
                fsa::CmpControl update{};
                update.cmd = fsa::CmpControlCmd::UPDATE;
                sa_io.cmp_ctrl = fsa::make_valid(update);
            }

            fsa::PECtrl flow_down{};
            flow_down.flow_ud = true;

            /*
             * 对应AttentionScoreExecPlan中的load_reg_ui.parallel。
             * compute_cycle=4时两行同时打开load_reg_ui：
             *   col0本拍保存S01和S00；
             *   同一控制经过横向Pipe后，col1下一拍保存S11和S10。
             * flow_ud仍然保持为true，所以保存S不会打断它继续向底部传播。
             */
            flow_down.load_reg_ui = compute_cycle==4;
            if(compute_cycle>=3 && compute_cycle<=4){
                sa_io.pe_ctrl[0] = fsa::make_valid(flow_down);
            }
            if(compute_cycle>=4 && compute_cycle<=5){
                sa_io.pe_ctrl[1] = fsa::make_valid(flow_down);
            }

            if(compute_cycle<=2){
                stage = "K经InputDelayer错拍，PE从下向上计算QK";
            }else if(compute_cycle==3){
                stage = "CMP更新max，并把S从顶部送回PE";
            }else if(compute_cycle==4){
                stage = "S向下回流，并存入第0列PE.reg";
            }else if(compute_cycle==5){
                stage = "S存入第1列PE.reg，并继续流向底部";
            }else{
                stage = "底部S经过OutputDelayer重新对齐";
            }
        }

        inputDelayer2Step(input_current, input_next, input_io);
        sa_io.pe_data[0] = input_io.out[0];
        sa_io.pe_data[1] = input_io.out[1];
        systolicArray2Step(sa_current, sa_next, sa_io);

        /*
         * 正式OutputDelayer没有valid端口。为了让trace不显示无意义的nan，
         * testbench将acc_out.valid=false对应的bits替换为0。
         */
        for(int col=0; col<TEST_COLS; ++col){
            output_io.in[col] = sa_io.acc_out[col].valid
                ? sa_io.acc_out[col].bits
                : fsa::accZero();
        }
        outputDelayer2Step(output_current, output_next, output_io);

        const int compute_cycle = cycle-3;
        if(compute_cycle==5){
            assert(sa_io.acc_out[0].valid);
            assert(almostEqual(sa_io.acc_out[0].bits, expected[0][0]));
            assert(!sa_io.acc_out[1].valid);
        }else if(compute_cycle==6){
            assert(sa_io.acc_out[0].valid && sa_io.acc_out[1].valid);
            assert(almostEqual(sa_io.acc_out[0].bits, expected[0][1]));
            assert(almostEqual(sa_io.acc_out[1].bits, expected[1][0]));
            result[0][0] = output_io.out[0];
            result[1][0] = output_io.out[1];
        }else if(compute_cycle==7){
            assert(!sa_io.acc_out[0].valid && sa_io.acc_out[1].valid);
            assert(almostEqual(sa_io.acc_out[1].bits, expected[1][1]));
            result[0][1] = output_io.out[0];
            result[1][1] = output_io.out[1];
        }

        printCycle(
            cycle,
            stage,
            input_io,
            input_next,
            sa_current,
            sa_next,
            sa_io,
            output_io,
            output_next);

        input_current = input_next;
        sa_current = sa_next;
        output_current = output_next;

        if(cycle==2){
            assert(almostEqual(sa_current.mesh[0][0].reg, q[0][0]));
            assert(almostEqual(sa_current.mesh[1][0].reg, q[0][1]));
            assert(almostEqual(sa_current.mesh[0][1].reg, q[1][0]));
            assert(almostEqual(sa_current.mesh[1][1].reg, q[1][1]));
        }else if(compute_cycle==4){
            // 第0列先保存query0对应的两个S，行顺序在PE中是反的。
            assert(almostEqual(sa_current.mesh[0][0].reg, expected[0][1]));
            assert(almostEqual(sa_current.mesh[1][0].reg, expected[0][0]));
        }else if(compute_cycle==5){
            // load_reg_ui控制向右走一拍后，第1列也保存完成。
            assert(almostEqual(sa_current.mesh[0][1].reg, expected[1][1]));
            assert(almostEqual(sa_current.mesh[1][1].reg, expected[1][0]));
        }
    }

    for(int row=0; row<TEST_ROWS; ++row){
        for(int col=0; col<TEST_COLS; ++col){
            assert(almostEqual(result[row][col], expected[row][col]));
        }
    }

    // FSA保存S后的布局：PE[row][col].reg=S[col][TEST_ROWS-1-row]。
    assert(almostEqual(sa_current.mesh[0][0].reg, expected[0][1]));
    assert(almostEqual(sa_current.mesh[1][0].reg, expected[0][0]));
    assert(almostEqual(sa_current.mesh[0][1].reg, expected[1][1]));
    assert(almostEqual(sa_current.mesh[1][1].reg, expected[1][0]));

    std::cout << "\n============================================================\n";
    std::cout << "OutputDelayer对齐后的S = Q*K^T\n";
    for(int row=0; row<TEST_ROWS; ++row){
        std::cout << "  [" << result[row][0] << ", " << result[row][1] << "]\n";
    }

    return 0;
}
