/**
 * @file test_systolic_array_top.cpp
 * @brief 4x4 SystolicArray HLS顶层完整矩阵乘测试，不调用Delayer。
 *
 * 本测试完成三件事：
 * 1. 使用load_reg_li把Q装入4x4 PE阵列；
 * 2. 在testbench中手动生成InputDelayer应产生的阶梯错拍K数据；
 * 3. 计算S=Q*K^T，经CMP原样回送并从SA底部读取全部16个结果。
 */
#include <cmath>
#include <iostream>
#include <string>

#include "fsa/hls/systolic_array_top.hpp"

namespace {

constexpr int TEST_SIZE = 4;

static_assert(fsa::SA_ROWS==TEST_SIZE,
              "test_systolic_array_top requires SA_ROWS=4");
static_assert(fsa::SA_COLS==TEST_SIZE,
              "test_systolic_array_top requires SA_COLS=4");

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

bool almostEqual(const fsa::acc_t actual, const float expected){
    return std::fabs((float)actual-expected)<0.05F;
}

void calculateGolden(float golden[TEST_SIZE][TEST_SIZE]){
    for(int query=0; query<TEST_SIZE; ++query){
        for(int key=0; key<TEST_SIZE; ++key){
            golden[query][key] = 0.0F;
            for(int feature=0; feature<TEST_SIZE; ++feature){
                golden[query][key] += Q[query][feature]*K[key][feature];
            }
        }
    }
}

/** @brief 调用一次SA顶层；一次调用对应一次HLS事务。 */
fsa::SystolicArrayOutput runSA(const fsa::SystolicArrayInput& input){
    fsa::SystolicArrayOutput output{};
    systolic_array_top(input, output);
    return output;
}

void resetSA(){
    fsa::SystolicArrayInput input{};
    input.reset = true;
    const fsa::SystolicArrayOutput output = runSA(input);

    for(int col=0; col<TEST_SIZE; ++col){
        expect(!output.acc_out[col].valid,
               "reset: acc_out["+std::to_string(col)+"] should be invalid");
    }
}

/**
 * @brief 将Q装入PE.reg。
 *
 * Q按query编号倒序送入。外部连续发送4拍load_reg_li，随后再空走3拍，
 * 让最后的控制和数据传播到最右侧PE。
 */
void loadStationaryQ(){
    fsa::PECtrl load{};
    load.load_reg_li = true;

    for(int cycle=0; cycle<TEST_SIZE; ++cycle){
        fsa::SystolicArrayInput input{};
        const int query = TEST_SIZE-1-cycle;

        for(int row=0; row<TEST_SIZE; ++row){
            input.pe_data[(std::size_t)row] = (fsa::elem_t)Q[query][row];
            input.pe_ctrl[row] = fsa::make_valid(load);
        }
        runSA(input);
    }

    for(int cycle=0; cycle<TEST_SIZE-1; ++cycle){
        runSA(fsa::SystolicArrayInput{});
    }
}

/**
 * @brief 构造一拍PE控制。
 *
 * mac_active让K沿水平方向传播并从下向上累加；flow_down_active
 * 同时把已经经过CMP的结果从上向下送到阵列底部。两条数据通路可以同拍工作。
 */
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

void testMatrixMultiply(){
    float golden[TEST_SIZE][TEST_SIZE]{};
    float result[TEST_SIZE][TEST_SIZE]{};
    bool received[TEST_SIZE][TEST_SIZE]{};
    calculateGolden(golden);

    resetSA();
    loadStationaryQ();

    // 最后一个结果从底部出现于compute_cycle=4*TEST_SIZE-1。
    constexpr int LAST_COMPUTE_CYCLE = 4*TEST_SIZE-1;

    for(int cycle=0; cycle<=LAST_COMPUTE_CYCLE; ++cycle){
        fsa::SystolicArrayInput input{};

        // 每个K行的点积到达顶部后，下一拍由对应CMP执行UPDATE并原样回送。
        if(cycle>=TEST_SIZE && cycle<2*TEST_SIZE){
            fsa::CmpControl cmp{};
            cmp.cmd = fsa::CmpControlCmd::UPDATE;
            input.cmp_ctrl = fsa::make_valid(cmp);
        }

        for(int row=0; row<TEST_SIZE; ++row){
            // 手动产生InputDelayer的效果：第row路延迟TEST_SIZE-1-row拍。
            const int key = cycle-(TEST_SIZE-1-row);
            const bool mac_active = key>=0 && key<TEST_SIZE;

            if(mac_active){
                input.pe_data[(std::size_t)row] = (fsa::elem_t)K[key][row];
            }

            // CMP回送的数据形成向下波前，第row行比第0行晚row拍启动。
            const int first_flow_cycle = TEST_SIZE+1+row;
            const bool flow_down_active =
                cycle>=first_flow_cycle && cycle<first_flow_cycle+TEST_SIZE;

            input.pe_ctrl[row] = makePECtrl(mac_active, flow_down_active);
        }

        const fsa::SystolicArrayOutput output = runSA(input);

        for(int col=0; col<TEST_SIZE; ++col){
            if(!output.acc_out[col].valid){
                continue;
            }

            // 第col列相对第0列多col拍横向延迟。
            const int key = cycle-(2*TEST_SIZE+1)-col;
            expect(key>=0 && key<TEST_SIZE,
                   "unexpected valid output at cycle "+std::to_string(cycle)+
                   ", col "+std::to_string(col));

            if(key>=0 && key<TEST_SIZE){
                result[col][key] = (float)output.acc_out[col].bits;
                received[col][key] = true;
            }
        }
    }

    for(int query=0; query<TEST_SIZE; ++query){
        for(int key=0; key<TEST_SIZE; ++key){
            expect(received[query][key],
                   "missing S["+std::to_string(query)+"]["+
                   std::to_string(key)+"]");
            expect(almostEqual((fsa::acc_t)result[query][key],
                               golden[query][key]),
                   "wrong S["+std::to_string(query)+"]["+
                   std::to_string(key)+"]");
        }
    }

    std::cout << "S = Q * K^T" << std::endl;
    for(int row=0; row<TEST_SIZE; ++row){
        std::cout << "  [";
        for(int col=0; col<TEST_SIZE; ++col){
            std::cout << result[row][col];
            if(col+1<TEST_SIZE){
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;
    }
}

}  // namespace

int main(){
    testMatrixMultiply();

    if(failure_count!=0){
        std::cerr << "[FAIL] test_systolic_array_top: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_systolic_array_top: 4x4 complete test"
              << std::endl;
    return 0;
}
