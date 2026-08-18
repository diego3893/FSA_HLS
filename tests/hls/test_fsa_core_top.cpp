/**
 * @file test_fsa_core_top.cpp
 * @brief FSA核心顶层的端到端逻辑step、SRAM响应和RMW对齐测试
 */

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "fsa/arithmetic.hpp"
#include "fsa/hls/fsa_core_top.hpp"

namespace{

    constexpr int TEST_SIZE = 4;
    constexpr int Q_BASE_ADDRESS = 0;
    constexpr int K_BASE_ADDRESS = TEST_SIZE;

    static_assert(fsa::SA_ROWS==TEST_SIZE && fsa::SA_COLS==TEST_SIZE,
                  "test_fsa_core_top requires a 4x4 SA");
    static_assert(fsa::SPAD_SUB_BANKS==1,
                  "current end-to-end schedule assumes one Scratchpad sub-bank");
    static_assert(fsa::ACC_SUB_BANKS==2,
                  "current accRAM readback assumes two sub-banks");

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
    int logical_cycle = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            // tick()返回前已经把logical_cycle加一；失败应标记刚完成的拍。
            std::cerr << "[FAIL] logical cycle " << logical_cycle-1
                      << ": " << message << std::endl;
            ++failure_count;
        }
    }

    bool almostEqual(const float actual, const float expected){
        return std::fabs(actual-expected)<0.05F;
    }

    void printAccVector(const fsa::AccVector& data){
        std::cout << '[';
        for(int col=0; col<TEST_SIZE; ++col){
            std::cout << data[(std::size_t)col];
            if(col+1<TEST_SIZE){
                std::cout << ',';
            }
        }
        std::cout << ']';
    }

    void printElemVector(const fsa::ElemVector& data){
        std::cout << '[';
        for(int row=0; row<TEST_SIZE; ++row){
            std::cout << (float)data[(std::size_t)row];
            if(row+1<TEST_SIZE){
                std::cout << ',';
            }
        }
        std::cout << ']';
    }

    void printTrace(
        const fsa::FsaCoreTopInput& input,
        const fsa::FsaCoreTopOutput& output
    ){
        std::cout << "[TRACE] cycle=" << logical_cycle
                  << " sp(v/a/c/r)=" << input.sp_read.valid
                  << '/' << input.sp_read.addr.to_uint()
                  << '/' << input.sp_read.is_constant
                  << '/' << output.sp_read_ready
                  << " delayer=";
        printElemVector(output.delayer_out);
        std::cout << " aligned=";
        printAccVector(output.aligned_sa_out);
        std::cout << " acc(v/a/rmw/r)=" << input.acc_read.valid
                  << '/' << input.acc_read.addr.to_uint()
                  << '/' << input.acc_read.rmw
                  << '/' << output.acc_read_ready
                  << " acc_ctrl=" << input.acc_ctrl.valid;
        if(input.acc_ctrl.valid){
            std::cout << ':'
                      << static_cast<int>(input.acc_ctrl.bits.cmd);
        }
        std::cout << " write(v/a/r)=" << output.acc_write_valid
                  << '/' << output.acc_write_addr.to_uint()
                  << '/' << output.acc_write_ready
                  << " acc_out=";
        printAccVector(output.accumulator_out);
        std::cout << " dma_resp=";
        for(int port=0; port<fsa::nMemPorts; ++port){
            std::cout << output.acc_dma_response_valid[port];
        }
        std::cout << std::endl;
    }

    fsa::FsaCoreTopOutput tick(const fsa::FsaCoreTopInput& input){
        fsa::FsaCoreTopOutput output{};
        fsa_core_top(input, output);
        printTrace(input, output);
        ++logical_cycle;
        return output;
    }

    void resetCore(){
        fsa::FsaCoreTopInput input{};
        input.reset = true;
        const fsa::FsaCoreTopOutput output = tick(input);

        expect(!output.sp_read_ready, "reset asserted sp_read_ready");
        expect(!output.acc_read_ready, "reset asserted acc_read_ready");
        expect(!output.acc_write_valid, "reset left an RMW write pending");
        for(int port=0; port<fsa::nMemPorts; ++port){
            expect(!output.acc_dma_response_valid[port],
                   "reset left a DMA response pending");
        }
    }

    void writeSpadRow(
        const int address,
        const float row[TEST_SIZE]
    ){
        constexpr int ELEMENTS_PER_SUB_BANK =
            fsa::SA_ROWS/fsa::SPAD_SUB_BANKS;

        for(int sub_bank=0; sub_bank<fsa::SPAD_SUB_BANKS; ++sub_bank){
            fsa::FsaCoreTopInput input{};
            input.spad_write_valid[0] = true;
            input.spad_write_addr[0] = address;
            input.spad_write_sub_bank[0] = sub_bank;
            for(int element=0; element<ELEMENTS_PER_SUB_BANK; ++element){
                const int row_element =
                    sub_bank*ELEMENTS_PER_SUB_BANK+element;
                input.spad_write_data[0][element] =
                    (fsa::elem_t)row[row_element];
            }

            const fsa::FsaCoreTopOutput output = tick(input);
            expect(output.spad_write_ready[0],
                   "Scratchpad preload was backpressured");
        }
    }

    void preloadMatrices(){
        for(int row=0; row<TEST_SIZE; ++row){
            writeSpadRow(Q_BASE_ADDRESS+row, Q[row]);
            writeSpadRow(K_BASE_ADDRESS+row, K[row]);
        }
    }

    void setSpRead(
        fsa::FsaCoreTopInput& input,
        const int address,
        const bool delayed_layout
    ){
        input.sp_read.valid = true;
        input.sp_read.addr = address;
        input.sp_read.rev_sram_out = delayed_layout;
        input.sp_read.delay_sram_out = delayed_layout;
        input.sp_read.rev_delayer_out = delayed_layout;
    }

    fsa::ValidData<fsa::PECtrl> makePECtrl(
        const bool mac_active,
        const bool flow_down_active
    ){
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

    void calculateGolden(
        float golden[TEST_SIZE][TEST_SIZE],
        float rowmax[TEST_SIZE]
    ){
        for(int query=0; query<TEST_SIZE; ++query){
            rowmax[query] = -1.0e30F;
            for(int key=0; key<TEST_SIZE; ++key){
                golden[query][key] = 0.0F;
                for(int feature=0; feature<TEST_SIZE; ++feature){
                    golden[query][key] +=
                        Q[query][feature]*K[key][feature];
                }
                if(golden[query][key]>rowmax[query]){
                    rowmax[query] = golden[query][key];
                }
            }
        }
    }

    /** @brief 检查常量值和布局控制都在请求拍锁存。 */
    void testConstantResponseAlignment(){
        resetCore();

        fsa::FsaCoreTopInput request{};
        request.sp_read.valid = true;
        request.sp_read.is_constant = true;
        request.sp_constant_value = (fsa::elem_t)7.0F;
        const fsa::FsaCoreTopOutput request_output = tick(request);
        expect(request_output.sp_read_ready,
               "Scratchpad constant request was not accepted");

        fsa::FsaCoreTopInput response{};
        response.sp_constant_value = (fsa::elem_t)99.0F;
        const fsa::FsaCoreTopOutput response_output = tick(response);
        for(int row=0; row<TEST_SIZE; ++row){
            expect((float)response_output.delayer_out[(std::size_t)row]
                       ==7.0F,
                   "Scratchpad constant was not held until response step");
        }
    }

    /** @brief Q通过Scratchpad和InputDelayer直通后装入PE的stationary寄存器。 */
    void loadStationaryQ(){
        fsa::FsaCoreTopInput prefetch{};
        setSpRead(prefetch, Q_BASE_ADDRESS+TEST_SIZE-1, false);
        const fsa::FsaCoreTopOutput prefetch_output = tick(prefetch);
        expect(prefetch_output.sp_read_ready,
               "first Q prefetch was backpressured");

        fsa::PECtrl load{};
        load.load_reg_li = true;

        for(int cycle=0; cycle<TEST_SIZE; ++cycle){
            fsa::FsaCoreTopInput input{};
            if(cycle+1<TEST_SIZE){
                setSpRead(
                    input,
                    Q_BASE_ADDRESS+TEST_SIZE-2-cycle,
                    false
                );
            }
            for(int row=0; row<TEST_SIZE; ++row){
                input.pe_ctrl[row] = fsa::make_valid(load);
            }

            const fsa::FsaCoreTopOutput output = tick(input);
            if(input.sp_read.valid){
                expect(output.sp_read_ready,
                       "Q prefetch was backpressured");
            }

            const int query = TEST_SIZE-1-cycle;
            for(int row=0; row<TEST_SIZE; ++row){
                expect(almostEqual(
                           (float)output.delayer_out[(std::size_t)row],
                           Q[query][row]),
                       "wrong Q row from Scratchpad at load cycle "+
                           std::to_string(cycle));
            }
        }

        // 排空SA横向控制pipe。此时数据位允许保持旧值；控制valid无效，
        // 所以这些数据不会被PE消费，也不要求InputDelayer输出为零。
        for(int cycle=0; cycle<TEST_SIZE-1; ++cycle){
            tick(fsa::FsaCoreTopInput{});
        }
    }

    fsa::AccVector readAccRow(const int address){
        fsa::FsaCoreTopInput request{};
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            request.acc_dma_read_valid[sub_bank] = true;
            request.acc_dma_read_addr[sub_bank] = address;
            request.acc_dma_read_sub_bank[sub_bank] = sub_bank;
        }

        const fsa::FsaCoreTopOutput request_output = tick(request);
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            expect(request_output.acc_dma_read_ready[sub_bank],
                   "accRAM narrow read was backpressured");
        }

        const fsa::FsaCoreTopOutput response =
            tick(fsa::FsaCoreTopInput{});
        fsa::AccVector row{};
        constexpr int ELEMENTS_PER_SUB_BANK =
            fsa::SA_COLS/fsa::ACC_SUB_BANKS;
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            expect(response.acc_dma_response_valid[sub_bank],
                   "accRAM narrow response-valid was not delayed one step");
            for(int element=0; element<ELEMENTS_PER_SUB_BANK; ++element){
                const int row_element =
                    sub_bank*ELEMENTS_PER_SUB_BANK+element;
                row[(std::size_t)row_element] =
                    response.acc_dma_read_data[sub_bank][element];
            }
        }
        return row;
    }

    void testEndToEndAttentionScoreRmw(){
        float golden[TEST_SIZE][TEST_SIZE]{};
        float golden_rowmax[TEST_SIZE]{};
        float aligned_result[TEST_SIZE][TEST_SIZE]{};
        bool aligned_received[TEST_SIZE][TEST_SIZE]{};
        calculateGolden(golden, golden_rowmax);

        resetCore();
        preloadMatrices();
        loadStationaryQ();

        // 预取K[0]；下一次tick才会进入InputDelayer。
        fsa::FsaCoreTopInput prefetch{};
        setSpRead(prefetch, K_BASE_ADDRESS, true);
        const fsa::FsaCoreTopOutput prefetch_output = tick(prefetch);
        expect(prefetch_output.sp_read_ready,
               "first K prefetch was backpressured");

        constexpr int LAST_COMPUTE_CYCLE = 4*TEST_SIZE;
        for(int cycle=0; cycle<=LAST_COMPUTE_CYCLE; ++cycle){
            fsa::FsaCoreTopInput input{};

            if(cycle+1<TEST_SIZE){
                setSpRead(input, K_BASE_ADDRESS+cycle+1, true);
            }

            if(cycle>=TEST_SIZE && cycle<2*TEST_SIZE){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::UPDATE;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }else if(cycle==2*TEST_SIZE){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::PROP_MAX;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }

            for(int row=0; row<TEST_SIZE; ++row){
                const int key = cycle-(TEST_SIZE-1-row);
                const bool mac_active = key>=0 && key<TEST_SIZE;
                const int first_flow_cycle = TEST_SIZE+1+row;
                const bool flow_down_active =
                    cycle>=first_flow_cycle &&
                    cycle<first_flow_cycle+TEST_SIZE+1;
                input.pe_ctrl[row] =
                    makePECtrl(mac_active, flow_down_active);

                if(cycle==2*TEST_SIZE){
                    input.pe_ctrl[row].valid = true;
                    input.pe_ctrl[row].bits.load_reg_ui = true;
                }
            }

            // 提前一拍发ZERO常量RMW请求，使下一拍对齐的S写入accRAM。
            if(cycle>=3*TEST_SIZE-1 && cycle<4*TEST_SIZE-1){
                input.acc_read.valid = true;
                input.acc_read.is_constant = true;
                input.acc_read.addr = cycle-(3*TEST_SIZE-1);
                input.acc_read.rmw = true;
                input.acc_constant_value = (fsa::acc_t)0.0F;
            }
            if(cycle>=3*TEST_SIZE && cycle<4*TEST_SIZE){
                fsa::AccumulatorControl ctrl{};
                ctrl.cmd = fsa::AccumulatorCmd::ACC_SA;
                input.acc_ctrl = fsa::make_valid(ctrl);
            }

            const fsa::FsaCoreTopOutput output = tick(input);
            if(input.sp_read.valid){
                expect(output.sp_read_ready,
                       "K prefetch was backpressured");
            }
            if(input.acc_read.valid){
                expect(output.acc_read_ready,
                       "Accumulator constant request was backpressured");
            }

            for(int row=0; row<TEST_SIZE; ++row){
                const int key = cycle-(TEST_SIZE-1-row);
                const bool mac_active = key>=0 && key<TEST_SIZE;

                // valid=false时数据位是don't-care，可能保留Scratchpad上一行。
                // 只检查本拍真正由PE控制消费、参与MAC的Delayer lane。
                if(mac_active){
                    expect(almostEqual(
                               (float)output.delayer_out[(std::size_t)row],
                               K[key][row]),
                           "wrong active delayed K at compute cycle "+
                               std::to_string(cycle)+", row "+
                               std::to_string(row));
                }
            }

            if(cycle>=3*TEST_SIZE && cycle<4*TEST_SIZE){
                const int key = cycle-3*TEST_SIZE;
                expect(output.acc_write_valid,
                       "RMW write-valid missing for key "+
                           std::to_string(key));
                expect(output.acc_write_ready,
                       "RMW write was backpressured for key "+
                           std::to_string(key));
                expect((int)output.acc_write_addr.to_uint()==key,
                       "RMW write used the wrong delayed address");

                for(int query=0; query<TEST_SIZE; ++query){
                    aligned_result[query][key] = (float)fsa::viewAasE(
                        output.aligned_sa_out[(std::size_t)query]
                    );
                    aligned_received[query][key] = true;
                    expect(almostEqual(
                               aligned_result[query][key],
                               golden[query][key]),
                           "wrong aligned S["+
                               std::to_string(query)+"]["+
                               std::to_string(key)+"]");
                }
            }else{
                expect(!output.acc_write_valid,
                       "unexpected RMW write outside result window");
            }

            if(cycle==4*TEST_SIZE){
                for(int query=0; query<TEST_SIZE; ++query){
                    const float rowmax =
                        -output.aligned_sa_out[(std::size_t)query];
                    expect(almostEqual(rowmax, golden_rowmax[query]),
                           "wrong aligned rowmax["+
                               std::to_string(query)+"]");
                }
            }
        }

        for(int query=0; query<TEST_SIZE; ++query){
            for(int key=0; key<TEST_SIZE; ++key){
                expect(aligned_received[query][key],
                       "missing aligned result");
            }
        }

        // 每个accRAM行保存一个key对应的四个query结果。
        for(int key=0; key<TEST_SIZE; ++key){
            const fsa::AccVector stored = readAccRow(key);
            for(int query=0; query<TEST_SIZE; ++query){
                const float decoded = (float)fsa::viewAasE(
                    stored[(std::size_t)query]
                );
                expect(almostEqual(decoded, golden[query][key]),
                       "wrong accRAM RMW result at key "+
                           std::to_string(key)+", query "+
                           std::to_string(query));
            }
        }
    }

}  // namespace

int main(){
    testConstantResponseAlignment();
    testEndToEndAttentionScoreRmw();

    if(failure_count!=0){
        std::cerr << "[FAIL] test_fsa_core_top: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_fsa_core_top: Scratchpad -> InputDelayer -> "
                 "SystolicArray -> OutputDelayer -> Accumulator -> accRAM"
              << std::endl;
    return 0;
}
