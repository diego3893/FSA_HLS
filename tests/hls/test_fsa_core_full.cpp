/**
 * @file test_fsa_core_full.cpp
 * @brief 从装入Q/K/V到L与最终归一化O写回accRAM的完整4x4 FA测试
 *
 * Controller和ExecutionPlan尚未迁移，因此本测试按照原Chisel
 * ExecutionPlan逐逻辑step产生sp_read、PE、CMP和Accumulator控制。
 */

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "fsa/arithmetic.hpp"
#include "fsa/hls/fsa_core_top.hpp"

#ifdef FSA_LOCAL_MATH_STUBS
namespace hls{
    float fabs(const float value){ return std::fabs(value); }
    float fma(const float a, const float b, const float c){
        return std::fma(a, b, c);
    }
    float trunc(const float value){ return std::trunc(value); }
    float ldexp(const float value, const int exponent){
        return std::ldexp(value, exponent);
    }
}
#endif

namespace{

    constexpr int N = 4;
    constexpr int Q_BASE_ADDRESS = 0;
    constexpr int K_BASE_ADDRESS = Q_BASE_ADDRESS+N;
    constexpr int VT_BASE_ADDRESS = K_BASE_ADDRESS+N;
    constexpr int L_ADDRESS = 0;
    constexpr int O_BASE_ADDRESS = 1;

    static_assert(fsa::SA_ROWS==N && fsa::SA_COLS==N,
                  "test_fsa_core_full requires a 4x4 SA");
    static_assert(fsa::SPAD_SUB_BANKS==1,
                  "current full-FA schedule assumes one Scratchpad sub-bank");
    static_assert(fsa::ACC_SUB_BANKS==2,
                  "current accRAM readback assumes two sub-banks");
    static_assert(fsa::ACC_ROWS>=1+N,
                  "accRAM must contain one L row and four O rows");

    const float Q[N][N] = {
        {1, 2, 3, 4},
        {2, 1, 0, 1},
        {1, 0, 1, 0},
        {3, 2, 1, 0}
    };

    const float K[N][N] = {
        {1, 0, 2, 1},
        {0, 1, 1, 2},
        {2, 1, 0, 1},
        {1, 2, 1, 0}
    };

    // V按[key][output_feature]保存；写入Scratchpad时使用下面的V转置行。
    const float V[N][N] = {
        {1, 2, 3, 4},
        {2, 0, 1, 3},
        {0, 1, 2, 1},
        {3, 2, 0, 1}
    };

    // 原Chisel中PE按此顺序扫描8段FP16 exp2斜率。
    const fsa::elem_t EXP2_SLOPES[fsa::exp2PWLPieces] = {
        (fsa::elem_t)0.664062500F,
        (fsa::elem_t)0.608886719F,
        (fsa::elem_t)0.558105469F,
        (fsa::elem_t)0.512207031F,
        (fsa::elem_t)0.469482422F,
        (fsa::elem_t)0.430419922F,
        (fsa::elem_t)0.394775391F,
        (fsa::elem_t)0.362060547F
    };

    int failure_count = 0;
    int logical_cycle = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            std::cerr << "[FAIL] logical cycle " << logical_cycle-1
                      << ": " << message << std::endl;
            ++failure_count;
        }
    }

    bool almostEqual(
        const float actual,
        const float expected,
        const float tolerance
    ){
        return std::isfinite(actual) &&
               std::fabs(actual-expected)<=tolerance;
    }

    void printAccVector(const fsa::AccVector& data){
        std::cout << '[';
        for(int lane=0; lane<N; ++lane){
            std::cout << data[(std::size_t)lane];
            if(lane+1<N){
                std::cout << ',';
            }
        }
        std::cout << ']';
    }

    fsa::FsaCoreTopOutput tick(
        const fsa::FsaCoreTopInput& input,
        const char* phase,
        const int phase_cycle
    ){
        fsa::FsaCoreTopOutput output{};
        fsa_core_top(input, output);

        std::cout << "[TRACE] global=" << logical_cycle
                  << " phase=" << phase
                  << " pc=" << phase_cycle
                  << " sp=" << input.sp_read.valid
                  << '/' << input.sp_read.addr.to_uint()
                  << '/' << input.sp_read.is_constant
                  << '/' << output.sp_read_ready
                  << " cmp=" << input.cmp_ctrl.valid;
        if(input.cmp_ctrl.valid){
            std::cout << ':'
                      << static_cast<int>(input.cmp_ctrl.bits.cmd);
        }
        std::cout << " acc_read=" << input.acc_read.valid
                  << '/' << input.acc_read.addr.to_uint()
                  << '/' << input.acc_read.rmw
                  << '/' << output.acc_read_ready
                  << " acc_ctrl=" << input.acc_ctrl.valid;
        if(input.acc_ctrl.valid){
            std::cout << ':'
                      << static_cast<int>(input.acc_ctrl.bits.cmd);
        }
        std::cout << " write=" << output.acc_write_valid
                  << '/' << output.acc_write_addr.to_uint()
                  << '/' << output.acc_write_ready
                  << " aligned=";
        printAccVector(output.aligned_sa_out);
        std::cout << " acc_out=";
        printAccVector(output.accumulator_out);
        std::cout << std::endl;

        ++logical_cycle;
        if(input.sp_read.valid){
            expect(output.sp_read_ready,
                   std::string(phase)+" Scratchpad read was backpressured");
        }
        if(input.acc_read.valid){
            expect(output.acc_read_ready,
                   std::string(phase)+" accRAM read was backpressured");
        }
        return output;
    }

    void resetCore(){
        fsa::FsaCoreTopInput input{};
        input.reset = true;
        const fsa::FsaCoreTopOutput output = tick(input, "reset", 0);
        expect(!output.acc_write_valid,
               "reset left an accRAM write pending");
    }

    void writeSpadRow(
        const int address,
        const float row[N],
        const int preload_cycle
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

            const fsa::FsaCoreTopOutput output =
                tick(input, "preload", preload_cycle);
            expect(output.spad_write_ready[0],
                   "Scratchpad preload was backpressured");
        }
    }

    void preloadQKV(){
        int preload_cycle = 0;
        for(int row=0; row<N; ++row){
            writeSpadRow(Q_BASE_ADDRESS+row, Q[row], preload_cycle++);
        }
        for(int row=0; row<N; ++row){
            writeSpadRow(K_BASE_ADDRESS+row, K[row], preload_cycle++);
        }
        for(int output_feature=0; output_feature<N; ++output_feature){
            float transposed_row[N]{};
            for(int key=0; key<N; ++key){
                transposed_row[key] = V[key][output_feature];
            }
            writeSpadRow(
                VT_BASE_ADDRESS+output_feature,
                transposed_row,
                preload_cycle++
            );
        }
    }

    void setSpRead(
        fsa::FsaCoreTopInput& input,
        const int address,
        const bool rev_input,
        const bool delay_output,
        const bool rev_output
    ){
        input.sp_read.valid = true;
        input.sp_read.addr = address;
        input.sp_read.rev_sram_out = rev_input;
        input.sp_read.delay_sram_out = delay_output;
        input.sp_read.rev_delayer_out = rev_output;
    }

    void setSpConstant(
        fsa::FsaCoreTopInput& input,
        const fsa::elem_t value
    ){
        input.sp_read.valid = true;
        input.sp_read.is_constant = true;
        input.sp_read.delay_sram_out = true;
        input.sp_constant_value = value;
    }

    void setAccumulatorControl(
        fsa::FsaCoreTopInput& input,
        const fsa::AccumulatorCmd cmd
    ){
        fsa::AccumulatorControl ctrl{};
        ctrl.cmd = cmd;
        input.acc_ctrl = fsa::make_valid(ctrl);
    }

    bool flowUp(
        const int cycle,
        const int row,
        const int start,
        const int repeat
    ){
        const int lane_start = start+(N-1-row);
        return cycle>=lane_start && cycle<lane_start+repeat;
    }

    bool flowDown(
        const int cycle,
        const int row,
        const int start,
        const int repeat
    ){
        const int lane_start = start+row;
        return cycle>=lane_start && cycle<lane_start+repeat;
    }

    /** @brief LOAD_STATIONARY：将四个query向量装入4x4 PE.reg。 */
    void loadStationaryQ(){
        fsa::FsaCoreTopInput prefetch{};
        setSpRead(prefetch, Q_BASE_ADDRESS+N-1, false, false, false);
        tick(prefetch, "load_q", 0);

        for(int cycle=0; cycle<N; ++cycle){
            fsa::FsaCoreTopInput input{};
            if(cycle+1<N){
                setSpRead(
                    input,
                    Q_BASE_ADDRESS+N-2-cycle,
                    false,
                    false,
                    false
                );
            }

            fsa::PECtrl ctrl{};
            ctrl.load_reg_li = true;
            for(int row=0; row<N; ++row){
                input.pe_ctrl[row] = fsa::make_valid(ctrl);
            }
            tick(input, "load_q", cycle+1);
        }

        // 让最后一条装载控制和Q数据穿过其余SA列。
        for(int drain=0; drain<N-1; ++drain){
            tick(fsa::FsaCoreTopInput{}, "load_q_drain", drain);
        }
    }

    /** @brief ATTN_SCORE：计算S、P和L，并将L写入accRAM[0]。 */
    void runAttentionScore(){
        constexpr int EXP2_START = 2*N+4;
        constexpr int EXP2_END = EXP2_START+fsa::exp2PWLPieces-1;
        constexpr int LAST_CYCLE = EXP2_END+N+N;

        for(int cycle=0; cycle<=LAST_CYCLE; ++cycle){
            fsa::FsaCoreTopInput input{};

            if(cycle<N){
                setSpRead(
                    input,
                    K_BASE_ADDRESS+cycle,
                    true,
                    true,
                    true
                );
            }else if(cycle==2*N+1){
                setSpConstant(input, (fsa::elem_t)1.0F);
            }else if(cycle==2*N+2){
                setSpConstant(input, (fsa::elem_t)fsa::attentionScale());
            }else if(cycle>=EXP2_START-1 && cycle<EXP2_START-1+
                    fsa::exp2PWLPieces){
                setSpConstant(
                    input,
                    EXP2_SLOPES[cycle-(EXP2_START-1)]
                );
            }else if(cycle==EXP2_END){
                setSpConstant(input, (fsa::elem_t)1.0F);
            }

            if(cycle>=N+1 && cycle<N+1+N){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::UPDATE;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }else if(cycle==2*N+1){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::PROP_MAX;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }else if(cycle==2*N+2){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::PROP_MAX_DIFF;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }else if(cycle>=EXP2_START-1 && cycle<EXP2_START-1+
                    fsa::exp2PWLPieces){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::PROP_EXP2_INTERCEPTS;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }else if(cycle==EXP2_END){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::PROP_ZERO;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }

            for(int row=0; row<N; ++row){
                fsa::PECtrl ctrl{};

                ctrl.mac = flowUp(cycle, row, 1, N) ||
                           flowDown(cycle, row, EXP2_END+1, 1);
                ctrl.acc_ui = flowDown(cycle, row, 2*N+2, 1) ||
                              flowDown(
                                  cycle,
                                  row,
                                  EXP2_START,
                                  fsa::exp2PWLPieces
                              ) ||
                              flowDown(cycle, row, EXP2_END+1, 1);
                ctrl.load_reg_ui = cycle==2*N+1;
                ctrl.flow_lr = flowUp(cycle, row, 1, N) ||
                               flowDown(cycle, row, 2*N+2, 1) ||
                               flowDown(cycle, row, 2*N+3, 1) ||
                               flowDown(
                                   cycle,
                                   row,
                                   EXP2_START,
                                   fsa::exp2PWLPieces
                               ) ||
                               flowDown(cycle, row, EXP2_END+1, 1);
                ctrl.flow_ud = flowDown(cycle, row, N+1, N) ||
                               flowDown(cycle, row, 2*N+2, 1) ||
                               flowDown(cycle, row, 2*N+3, 1) ||
                               flowDown(
                                   cycle,
                                   row,
                                   EXP2_START,
                                   fsa::exp2PWLPieces
                               );
                ctrl.flow_du = flowUp(cycle, row, N+4, N);
                ctrl.update_reg = flowDown(cycle, row, 2*N+2, 1) ||
                                  flowDown(cycle, row, 2*N+3, 1);
                ctrl.exp2 = flowDown(
                    cycle,
                    row,
                    EXP2_START,
                    fsa::exp2PWLPieces
                );

                const bool valid = ctrl.mac || ctrl.acc_ui ||
                    ctrl.load_reg_ui || ctrl.flow_lr || ctrl.flow_ud ||
                    ctrl.flow_du || ctrl.update_reg || ctrl.exp2;
                if(valid){
                    input.pe_ctrl[row] = fsa::make_valid(ctrl);
                }
            }

            // 本测试只处理从空状态开始的第一个K/V block。旧L/O均由ZERO
            // 常量提供，Accumulator scale保持reset后的0即可；多block路径
            // 才需要EXP_S1/EXP_S2计算exp(oldMax-newMax)。
            if(cycle==EXP2_END+N+N){
                setAccumulatorControl(input, fsa::AccumulatorCmd::ACC_SA);
            }

            if(cycle==EXP2_END+N+N-1){
                input.acc_read.valid = true;
                input.acc_read.is_constant = true;
                input.acc_read.addr = L_ADDRESS;
                input.acc_read.rmw = true;
                input.acc_constant_value = (fsa::acc_t)0.0F;
            }

            const fsa::FsaCoreTopOutput output =
                tick(input, "score", cycle);
            if(cycle==LAST_CYCLE){
                expect(output.acc_write_valid,
                       "L RMW write-valid was missing");
                expect((int)output.acc_write_addr.to_uint()==L_ADDRESS,
                       "L RMW used the wrong address");
            }
        }
    }

    /** @brief ATTN_VALUE：计算P*V并把四个O分量行写入accRAM[1..4]。 */
    void runAttentionValue(){
        constexpr int READ_O_START = N+N-1;
        constexpr int ACC_O_START = N+N;
        constexpr int LAST_CYCLE = ACC_O_START+N-1;

        for(int cycle=0; cycle<=LAST_CYCLE; ++cycle){
            fsa::FsaCoreTopInput input{};
            if(cycle<N){
                setSpRead(
                    input,
                    VT_BASE_ADDRESS+cycle,
                    true,
                    true,
                    false
                );
            }

            for(int row=0; row<N; ++row){
                const bool active = flowDown(cycle, row, 1, N);
                if(active){
                    fsa::PECtrl ctrl{};
                    ctrl.mac = true;
                    ctrl.acc_ui = true;
                    ctrl.flow_lr = true;
                    input.pe_ctrl[row] = fsa::make_valid(ctrl);
                }
            }

            if(cycle>=READ_O_START && cycle<READ_O_START+N){
                input.acc_read.valid = true;
                input.acc_read.is_constant = true;
                input.acc_read.addr = O_BASE_ADDRESS+(cycle-READ_O_START);
                input.acc_read.rmw = true;
                input.acc_constant_value = (fsa::acc_t)0.0F;
            }
            if(cycle>=ACC_O_START && cycle<ACC_O_START+N){
                setAccumulatorControl(input, fsa::AccumulatorCmd::ACC_SA);
            }

            const fsa::FsaCoreTopOutput output =
                tick(input, "value", cycle);
            if(cycle>=ACC_O_START && cycle<ACC_O_START+N){
                const int expected_address =
                    O_BASE_ADDRESS+(cycle-ACC_O_START);
                expect(output.acc_write_valid,
                       "O RMW write-valid was missing");
                expect((int)output.acc_write_addr.to_uint()==
                           expected_address,
                       "O RMW used the wrong address");
            }
        }
    }

    fsa::AccVector readAccRow(const int address, const char* phase){
        fsa::FsaCoreTopInput request{};
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            request.acc_dma_read_valid[sub_bank] = true;
            request.acc_dma_read_addr[sub_bank] = address;
            request.acc_dma_read_sub_bank[sub_bank] = sub_bank;
        }

        const fsa::FsaCoreTopOutput request_output =
            tick(request, phase, address*2);
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            expect(request_output.acc_dma_read_ready[sub_bank],
                   std::string(phase)+" narrow read was backpressured");
        }

        const fsa::FsaCoreTopOutput response =
            tick(fsa::FsaCoreTopInput{}, phase, address*2+1);
        fsa::AccVector row{};
        constexpr int ELEMENTS_PER_SUB_BANK =
            fsa::SA_COLS/fsa::ACC_SUB_BANKS;
        for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
            expect(response.acc_dma_response_valid[sub_bank],
                   std::string(phase)+" narrow response-valid was missing");
            for(int element=0; element<ELEMENTS_PER_SUB_BANK; ++element){
                const int row_element =
                    sub_bank*ELEMENTS_PER_SUB_BANK+element;
                row[(std::size_t)row_element] =
                    response.acc_dma_read_data[sub_bank][element];
            }
        }
        return row;
    }

    /** @brief SET_SCALE + RECIPROCAL：把1/L保存进Accumulator scale。 */
    void runLseNormScale(){
        for(int cycle=0; cycle<fsa::reciprocalLatency+2; ++cycle){
            fsa::FsaCoreTopInput input{};
            if(cycle==0){
                input.acc_read.valid = true;
                input.acc_read.addr = L_ADDRESS;
                input.acc_read.rmw = false;
            }else if(cycle==1){
                setAccumulatorControl(input, fsa::AccumulatorCmd::SET_SCALE);
            }else{
                setAccumulatorControl(input, fsa::AccumulatorCmd::RECIPROCAL);
            }
            tick(input, "norm_scale", cycle);
        }
    }

    /** @brief ACC：用1/L归一化四个O行并原址写回。 */
    void runLseNorm(){
        constexpr int LAST_CYCLE = N;
        for(int cycle=0; cycle<=LAST_CYCLE; ++cycle){
            fsa::FsaCoreTopInput input{};
            if(cycle==0){
                fsa::CmpControl cmp{};
                cmp.cmd = fsa::CmpControlCmd::RESET;
                input.cmp_ctrl = fsa::make_valid(cmp);
            }
            if(cycle<N){
                input.acc_read.valid = true;
                input.acc_read.addr = O_BASE_ADDRESS+cycle;
                input.acc_read.rmw = true;
            }
            if(cycle>=1 && cycle<=N){
                setAccumulatorControl(input, fsa::AccumulatorCmd::ACC);
            }

            const fsa::FsaCoreTopOutput output =
                tick(input, "norm", cycle);
            if(cycle>=1){
                const int expected_address = O_BASE_ADDRESS+cycle-1;
                expect(output.acc_write_valid,
                       "normalized O write-valid was missing");
                expect((int)output.acc_write_addr.to_uint()==
                           expected_address,
                       "normalized O used the wrong write address");
            }
        }
    }

    void calculateGolden(
        float expected_l[N],
        float expected_o_numerator[N][N],
        float expected_o[N][N]
    ){
        for(int query=0; query<N; ++query){
            float score[N]{};
            float rowmax = -1.0e30F;
            for(int key=0; key<N; ++key){
                for(int feature=0; feature<N; ++feature){
                    score[key] += Q[query][feature]*K[key][feature];
                }
                if(score[key]>rowmax){
                    rowmax = score[key];
                }
            }

            float probability_numerator[N]{};
            expected_l[query] = 0.0F;
            for(int key=0; key<N; ++key){
                probability_numerator[key] =
                    std::exp((score[key]-rowmax)/std::sqrt((float)N));
                expected_l[query] += probability_numerator[key];
            }

            for(int output_feature=0; output_feature<N; ++output_feature){
                expected_o_numerator[query][output_feature] = 0.0F;
                for(int key=0; key<N; ++key){
                    expected_o_numerator[query][output_feature] +=
                        probability_numerator[key]*V[key][output_feature];
                }
                expected_o[query][output_feature] =
                    expected_o_numerator[query][output_feature]/
                    expected_l[query];
            }
        }
    }

    void checkVector(
        const char* name,
        const fsa::AccVector& actual,
        const float expected[N],
        const float tolerance
    ){
        for(int lane=0; lane<N; ++lane){
            expect(
                almostEqual(
                    actual[(std::size_t)lane],
                    expected[lane],
                    tolerance
                ),
                std::string(name)+" lane "+std::to_string(lane)+
                    " expected "+std::to_string(expected[lane])+
                    ", actual "+
                    std::to_string(actual[(std::size_t)lane])
            );
        }
    }

}  // namespace

int main(){
    float expected_l[N]{};
    float expected_o_numerator[N][N]{};
    float expected_o[N][N]{};
    calculateGolden(expected_l, expected_o_numerator, expected_o);

    resetCore();
    preloadQKV();
    loadStationaryQ();
    runAttentionScore();

    const fsa::AccVector stored_l = readAccRow(L_ADDRESS, "check_l");
    checkVector("L", stored_l, expected_l, 0.08F);

    runAttentionValue();
    for(int output_feature=0; output_feature<N; ++output_feature){
        const fsa::AccVector stored_o = readAccRow(
            O_BASE_ADDRESS+output_feature,
            "check_o_numerator"
        );
        float expected_row[N]{};
        for(int query=0; query<N; ++query){
            expected_row[query] =
                expected_o_numerator[query][output_feature];
        }
        checkVector("O numerator", stored_o, expected_row, 0.12F);
    }

    runLseNormScale();
    runLseNorm();

    for(int output_feature=0; output_feature<N; ++output_feature){
        const fsa::AccVector stored_o = readAccRow(
            O_BASE_ADDRESS+output_feature,
            "check_o"
        );
        float expected_row[N]{};
        for(int query=0; query<N; ++query){
            expected_row[query] = expected_o[query][output_feature];
        }
        checkVector("O", stored_o, expected_row, 0.08F);
    }

    if(failure_count!=0){
        std::cerr << "[FAIL] test_fsa_core_full: "
                  << failure_count << " check(s) failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] test_fsa_core_full: Q/K/V -> S -> P -> L -> "
                 "O numerator -> 1/L -> normalized O" << std::endl;
    return 0;
}
