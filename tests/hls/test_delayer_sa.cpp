/**
 * @file test_delayer_sa.cpp
 * @brief InputDelayer、4x4 SA和OutputDelayer联合AttentionScore测试。
 *
 * 本文件不复制三个模块的逻辑，而是逐拍调用正式顶层：
 * raw Q/K -> InputDelayer -> SystolicArray -> OutputDelayer。
 * 测试内容包括S=Q*K^T、CMP rowmax，以及SA底部错拍输出的重新对齐。
 */
#include <cmath>
#include <iostream>
#include <string>

#include "fsa/arithmetic.hpp"
#include "fsa/hls/input_delayer_top.hpp"
#include "fsa/hls/output_delayer_top.hpp"
#include "fsa/hls/systolic_array_top.hpp"

namespace {

constexpr int TEST_SIZE = 4;

static_assert(fsa::SA_ROWS==TEST_SIZE && fsa::SA_COLS==TEST_SIZE,
              "test_delayer_sa requires a 4x4 SA");

const float Q[TEST_SIZE][TEST_SIZE] = {
    {1, 2, 3, 4},
    {2, 1, 0, 1},
    {1, 0, 1, 0},
    {3, 2, 1, 0}
};

const float K[TEST_SIZE][TEST_SIZE] = {
    {1, 0, 2, 1},
    {0, 1, 1, 2},
    {2, 1, 0, 1},
    {1, 2, 1, 0}
};

int failure_count = 0;

void expect(const bool condition, const std::string& message){
    if(!condition){
        std::cerr << "[FAIL] " << message << std::endl;
        ++failure_count;
    }
}

bool almostEqual(const float actual, const float expected){
    return std::fabs(actual-expected)<0.05F;
}

void calculateGolden(float golden[TEST_SIZE][TEST_SIZE],
                     float rowmax[TEST_SIZE]){
    for(int query=0; query<TEST_SIZE; ++query){
        rowmax[query] = -1.0e30F;
        for(int key=0; key<TEST_SIZE; ++key){
            golden[query][key] = 0.0F;
            for(int feature=0; feature<TEST_SIZE; ++feature){
                golden[query][key] += Q[query][feature]*K[key][feature];
            }
            if(golden[query][key]>rowmax[query]){
                rowmax[query] = golden[query][key];
            }
        }
    }
}

fsa::InputDelayerTopOutput runDelayer(
    const fsa::InputDelayerTopInput& input){
    fsa::InputDelayerTopOutput output{};
    input_delayer_top(input, output);
    return output;
}

fsa::SystolicArrayOutput runSA(const fsa::SystolicArrayInput& input){
    fsa::SystolicArrayOutput output{};
    systolic_array_top(input, output);
    return output;
}

/**
 * @brief 把SA底部的Valid输出送进没有Valid端口的OutputDelayer。
 *
 * valid=false的列填0。真正有效的输出周期由testbench的控制时序确定。
 */
fsa::OutputDelayerTopOutput runOutputDelayer(
    const fsa::SystolicArrayOutput& sa_output){
    fsa::OutputDelayerTopInput input{};
    for(int col=0; col<TEST_SIZE; ++col){
        input.in[(std::size_t)col] = sa_output.acc_out[col].valid
            ? sa_output.acc_out[col].bits
            : fsa::acc_t{};
    }

    fsa::OutputDelayerTopOutput output{};
    output_delayer_top(input, output);
    return output;
}

void resetModules(){
    fsa::InputDelayerTopInput delayer_input{};
    delayer_input.reset = true;
    runDelayer(delayer_input);

    fsa::SystolicArrayInput sa_input{};
    sa_input.reset = true;
    runSA(sa_input);

    fsa::OutputDelayerTopInput output_delayer_input{};
    output_delayer_input.reset = true;
    fsa::OutputDelayerTopOutput output_delayer_output{};
    output_delayer_top(output_delayer_input, output_delayer_output);
}

fsa::ValidData<fsa::PECtrl> makePECtrl(const bool mac_active,
                                       const bool flow_down_active){
    if(!mac_active && !flow_down_active){
        return fsa::make_invalid<fsa::PECtrl>();
    }

    fsa::PECtrl ctrl{};
    ctrl.mac = mac_active;
    ctrl.acc_ui = false;
    ctrl.flow_lr = mac_active;
    ctrl.flow_ud = flow_down_active;
    return fsa::make_valid(ctrl);
}

/** @brief Q经InputDelayer直通后装入PE阵列。 */
void loadStationaryQ(){
    fsa::PECtrl load{};
    load.load_reg_li = true;

    for(int cycle=0; cycle<TEST_SIZE; ++cycle){
        fsa::InputDelayerTopInput delayer_input{};
        delayer_input.in.valid = true;

        const int query = TEST_SIZE-1-cycle;
        for(int row=0; row<TEST_SIZE; ++row){
            delayer_input.in.bits.data[(std::size_t)row] =
                (fsa::elem_t)Q[query][row];
        }

        const fsa::InputDelayerTopOutput delayer_output =
            runDelayer(delayer_input);

        fsa::SystolicArrayInput sa_input{};
        sa_input.pe_data = delayer_output.out;
        for(int row=0; row<TEST_SIZE; ++row){
            sa_input.pe_ctrl[row] = fsa::make_valid(load);
        }
        const fsa::SystolicArrayOutput sa_output = runSA(sa_input);
        runOutputDelayer(sa_output);
    }

    // 同时排空SA横向控制Pipe，并用0清空Delayer之前残留的延迟寄存器。
    for(int cycle=0; cycle<TEST_SIZE-1; ++cycle){
        const fsa::InputDelayerTopOutput delayer_output =
            runDelayer(fsa::InputDelayerTopInput{});
        fsa::SystolicArrayInput sa_input{};
        sa_input.pe_data = delayer_output.out;
        const fsa::SystolicArrayOutput sa_output = runSA(sa_input);
        runOutputDelayer(sa_output);
    }
}

void testJointMatrixMultiply(){
    float golden[TEST_SIZE][TEST_SIZE]{};
    float golden_rowmax[TEST_SIZE]{};
    float result[TEST_SIZE][TEST_SIZE]{};
    bool received[TEST_SIZE][TEST_SIZE]{};
    float result_rowmax[TEST_SIZE]{};
    bool rowmax_received = false;
    calculateGolden(golden, golden_rowmax);

    resetModules();
    loadStationaryQ();

    // cycle=3*TEST_SIZE到4*TEST_SIZE-1依次输出4个对齐后的key列；
    // cycle=4*TEST_SIZE输出4个同时对齐的-rowmax。
    constexpr int LAST_COMPUTE_CYCLE = 4*TEST_SIZE;

    for(int cycle=0; cycle<=LAST_COMPUTE_CYCLE; ++cycle){
        fsa::InputDelayerTopInput delayer_input{};

        // FSA计算QK时InputDelayer采用固定的反转、阶梯延迟、再反转。
        delayer_input.in.bits.rev_input = true;
        delayer_input.in.bits.delay_output = true;
        delayer_input.in.bits.rev_output = true;

        if(cycle<TEST_SIZE){
            delayer_input.in.valid = true;
            for(int feature=0; feature<TEST_SIZE; ++feature){
                delayer_input.in.bits.data[(std::size_t)feature] =
                    (fsa::elem_t)K[cycle][feature];
            }
        }

        const fsa::InputDelayerTopOutput delayer_output =
            runDelayer(delayer_input);

        fsa::SystolicArrayInput sa_input{};
        sa_input.pe_data = delayer_output.out;

        if(cycle>=TEST_SIZE && cycle<2*TEST_SIZE){
            fsa::CmpControl cmp{};
            cmp.cmd = fsa::CmpControlCmd::UPDATE;
            sa_input.cmp_ctrl = fsa::make_valid(cmp);
        }else if(cycle==2*TEST_SIZE){
            // CMP的PROP_MAX实际输出0-newMax，即-rowmax。
            fsa::CmpControl cmp{};
            cmp.cmd = fsa::CmpControlCmd::PROP_MAX;
            sa_input.cmp_ctrl = fsa::make_valid(cmp);
        }

        for(int row=0; row<TEST_SIZE; ++row){
            const int key = cycle-(TEST_SIZE-1-row);
            const bool mac_active = key>=0 && key<TEST_SIZE;

            // 同时检查Delayer输出是不是正确的K[key][row]错拍波前。
            const float expected_delayed = mac_active ? K[key][row] : 0.0F;
            expect(almostEqual((float)delayer_output.out[(std::size_t)row],
                               expected_delayed),
                   "wrong delayed data at cycle "+std::to_string(cycle)+
                   ", row "+std::to_string(row));

            const int first_flow_cycle = TEST_SIZE+1+row;
            const bool flow_down_active =
                cycle>=first_flow_cycle &&
                cycle<first_flow_cycle+TEST_SIZE+1;
            sa_input.pe_ctrl[row] = makePECtrl(mac_active, flow_down_active);

            // 在S全部回流到正确PE位置时，将S保存进PE.reg。
            // 这与AttentionScore执行计划中的load_reg_ui.parallel对应。
            if(cycle==2*TEST_SIZE){
                sa_input.pe_ctrl[row].valid = true;
                sa_input.pe_ctrl[row].bits.load_reg_ui = true;
            }
        }

        const fsa::SystolicArrayOutput sa_output = runSA(sa_input);
        const fsa::OutputDelayerTopOutput aligned_output =
            runOutputDelayer(sa_output);

        // OutputDelayer消除各SA列的错拍：同一拍得到一个key对应的4个query结果。
        if(cycle>=3*TEST_SIZE && cycle<4*TEST_SIZE){
            const int key = cycle-3*TEST_SIZE;
            for(int query=0; query<TEST_SIZE; ++query){
                // CMP UPDATE输出的是封装在acc_t低16位中的FP16位模式。
                // OutputDelayer只改变时序和顺序，不改变该编码，因此在这里解码。
                result[query][key] = (float)fsa::viewAasE(
                    aligned_output.out[(std::size_t)query]);
                received[query][key] = true;
            }
        }else if(cycle==4*TEST_SIZE){
            // PROP_MAX下发的是-rowmax，因此在testbench中取负恢复rowmax。
            for(int query=0; query<TEST_SIZE; ++query){
                result_rowmax[query] =
                    -(float)aligned_output.out[(std::size_t)query];
            }
            rowmax_received = true;
        }
    }

    expect(rowmax_received, "aligned rowmax was not received");
    for(int query=0; query<TEST_SIZE; ++query){
        expect(almostEqual(result_rowmax[query], golden_rowmax[query]),
               "wrong rowmax["+std::to_string(query)+"]");
    }

    std::cout << "Aligned S = Q * K^T" << std::endl;
    for(int query=0; query<TEST_SIZE; ++query){
        std::cout << "  [";
        for(int key=0; key<TEST_SIZE; ++key){
            std::cout << result[query][key];
            if(key+1<TEST_SIZE){
                std::cout << ", ";
            }
        }
        std::cout << "]  rowmax=" << result_rowmax[query] << std::endl;
    }

    for(int query=0; query<TEST_SIZE; ++query){
        for(int key=0; key<TEST_SIZE; ++key){
            expect(received[query][key],
                   "missing joint S["+std::to_string(query)+"]["+
                   std::to_string(key)+"]");
            expect(almostEqual(result[query][key], golden[query][key]),
                   "wrong joint S["+std::to_string(query)+"]["+
                   std::to_string(key)+"]");
        }
    }
}

}  // namespace

int main(){
    testJointMatrixMultiply();

    if(failure_count!=0){
        std::cerr << "[FAIL] test_delayer_sa: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_delayer_sa: InputDelayer + 4x4 SA + "
                 "OutputDelayer"
              << std::endl;
    return 0;
}
