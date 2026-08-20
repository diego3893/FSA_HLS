#include <cmath>
#include <iostream>
#include <string>

#include "fsa/arithmetic.hpp"
#include "fsa/execution_plan.hpp"

#ifdef FSA_EXECUTION_PLAN_STANDALONE
namespace fsa{
    acc_t attentionScale(){
        return 0.721347520F;
    }
}
#endif

namespace{

    int failure_count = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            std::cerr << "[FAIL] " << message << std::endl;
            ++failure_count;
        }
    }

    bool inRange(
        const unsigned timer,
        const unsigned start,
        const unsigned repeat
    ){
        return timer>=start && timer<start+repeat;
    }

    bool flowUp(
        const unsigned timer,
        const int row,
        const unsigned start,
        const unsigned repeat
    ){
        return inRange(timer, start+(fsa::SA_ROWS-1-row), repeat);
    }

    bool flowDown(
        const unsigned timer,
        const int row,
        const unsigned start,
        const unsigned repeat
    ){
        return inRange(timer, start+row, repeat);
    }

    fsa::MatrixInstruction instruction(const fsa::MxFunc function){
        fsa::MatrixInstruction value{};
        value.header.func = function;
        value.spad.addr = 10;
        value.spad.stride = -1;
        value.spad.revInput = true;
        value.spad.delayOutput = true;
        value.spad.revOutput = true;
        value.acc.addr = 1;
        value.acc.stride = 1;
        value.acc.causal = true;
        return value;
    }

    void checkLengths(){
        expect(
            fsa::execution_plan_length(
                instruction(fsa::MxFunc::LOAD_STATIONARY)
            )==5,
            "LOAD_STATIONARY length"
        );
        expect(
            fsa::execution_plan_length(
                instruction(fsa::MxFunc::ATTENTION_SCORE_COMPUTE)
            )==28,
            "ATTENTION_SCORE_COMPUTE length"
        );
        expect(
            fsa::execution_plan_length(
                instruction(fsa::MxFunc::ATTENTION_VALUE_COMPUTE)
            )==12,
            "ATTENTION_VALUE_COMPUTE length"
        );
        expect(
            fsa::execution_plan_length(
                instruction(fsa::MxFunc::ATTENTION_LSE_NORM_SCALE)
            )==17,
            "ATTENTION_LSE_NORM_SCALE length"
        );
        expect(
            fsa::execution_plan_length(
                instruction(fsa::MxFunc::ATTENTION_LSE_NORM)
            )==5,
            "ATTENTION_LSE_NORM length"
        );
    }

    void checkLoad(){
        const fsa::MatrixInstruction inst =
            instruction(fsa::MxFunc::LOAD_STATIONARY);
        for(unsigned timer=0; timer<5; ++timer){
            const fsa::ExecutionPlanStep step =
                fsa::make_execution_plan_step(inst, timer);
            expect(step.valid, "LOAD step valid");
            expect(step.first==(timer==0), "LOAD first");
            expect(step.last==(timer==4), "LOAD last");
            expect(
                step.semaphore_release==(timer==3),
                "LOAD semaphore release"
            );
            expect(step.conflict_free==(timer==3), "LOAD conflict-free");
            expect(step.sp_read.valid==(timer<4), "LOAD sp_read valid");
            if(timer<4){
                expect(
                    step.sp_read.addr.to_uint()==10-timer,
                    "LOAD descending spad address"
                );
                expect(step.sp_read.rev_sram_out, "LOAD revInput mapping");
            }
            for(int row=0; row<fsa::SA_ROWS; ++row){
                expect(
                    step.pe_ctrl[row].valid==(timer>=1),
                    "LOAD PE control valid"
                );
                if(step.pe_ctrl[row].valid){
                    expect(
                        step.pe_ctrl[row].bits.load_reg_li,
                        "LOAD load_reg_li"
                    );
                }
            }
        }
        expect(
            !fsa::make_execution_plan_step(inst, 5).valid,
            "LOAD out-of-range timer"
        );
    }

    void checkScore(){
        constexpr unsigned EXP2_START = 2*fsa::SA_ROWS+4;
        constexpr unsigned EXP2_END =
            EXP2_START+fsa::exp2PWLPieces-1;
        fsa::MatrixInstruction inst =
            instruction(fsa::MxFunc::ATTENTION_SCORE_COMPUTE);
        inst.spad.addr = 4;
        inst.spad.stride = 1;
        inst.acc.zero = true;

        for(unsigned timer=0; timer<28; ++timer){
            const fsa::ExecutionPlanStep step =
                fsa::make_execution_plan_step(inst, timer);
            expect(step.valid, "SCORE step valid");
            expect(step.last==(timer==27), "SCORE last");
            expect(
                step.semaphore_release==(timer==3),
                "SCORE semaphore release"
            );

            const bool expected_sp = timer<4 || timer==9 ||
                timer==10 || inRange(timer, 11, 8) || timer==19;
            expect(step.sp_read.valid==expected_sp, "SCORE sp_read table");
            if(timer<4){
                expect(
                    step.sp_read.addr.to_uint()==4+timer,
                    "SCORE K address"
                );
                expect(!step.sp_read.is_constant, "SCORE K is SRAM");
            }
            if(timer==9 || timer==19){
                expect(
                    std::fabs((float)step.sp_constant_value-1.0F)<0.001F,
                    "SCORE ONE constant"
                );
            }
            if(timer==10){
                expect(
                    std::fabs(
                        (float)step.sp_constant_value-
                        (float)(fsa::elem_t)fsa::attentionScale()
                    )<0.001F,
                    "SCORE attentionScale constant"
                );
            }

            const bool expected_cmp = inRange(timer, 5, 4) ||
                timer==9 || timer==10 || inRange(timer, 11, 8) ||
                timer==19;
            expect(step.cmp_ctrl.valid==expected_cmp, "SCORE CMP table");
            if(inRange(timer, 5, 4)){
                expect(
                    step.cmp_ctrl.bits.cmd==fsa::CmpControlCmd::UPDATE,
                    "SCORE CMP UPDATE"
                );
                expect(
                    step.cmp_ctrl.bits.causalCounter==timer-5,
                    "SCORE causal counter"
                );
            }

            for(int row=0; row<fsa::SA_ROWS; ++row){
                const fsa::PECtrl& control = step.pe_ctrl[row].bits;
                const bool mac = flowUp(timer, row, 1, 4) ||
                    flowDown(timer, row, 20, 1);
                const bool acc_ui = flowDown(timer, row, 10, 1) ||
                    flowDown(timer, row, 12, 8) ||
                    flowDown(timer, row, 20, 1);
                const bool load_reg_ui = timer==9;
                const bool flow_lr = flowUp(timer, row, 1, 4) ||
                    flowDown(timer, row, 10, 1) ||
                    flowDown(timer, row, 11, 1) ||
                    flowDown(timer, row, 12, 8) ||
                    flowDown(timer, row, 20, 1);
                const bool flow_ud = flowDown(timer, row, 5, 4) ||
                    flowDown(timer, row, 10, 1) ||
                    flowDown(timer, row, 11, 1) ||
                    flowDown(timer, row, 12, 8);
                const bool flow_du = flowUp(timer, row, 8, 4);
                const bool update_reg = flowDown(timer, row, 10, 1) ||
                    flowDown(timer, row, 11, 1);
                const bool exp2 = flowDown(timer, row, 12, 8);
                const bool valid = mac || acc_ui || load_reg_ui ||
                    flow_lr || flow_ud || flow_du || update_reg || exp2;

                expect(step.pe_ctrl[row].valid==valid, "SCORE PE valid");
                expect(control.mac==mac, "SCORE PE mac");
                expect(control.acc_ui==acc_ui, "SCORE PE acc_ui");
                expect(
                    control.load_reg_ui==load_reg_ui,
                    "SCORE PE load_reg_ui"
                );
                expect(control.flow_lr==flow_lr, "SCORE PE flow_lr");
                expect(control.flow_ud==flow_ud, "SCORE PE flow_ud");
                expect(control.flow_du==flow_du, "SCORE PE flow_du");
                expect(
                    control.update_reg==update_reg,
                    "SCORE PE update_reg"
                );
                expect(control.exp2==exp2, "SCORE PE exp2");
            }

            const bool expected_acc_ctrl = timer==27;
            expect(
                step.acc_ctrl.valid==expected_acc_ctrl,
                "SCORE accumulator table"
            );
            expect(step.acc_read.valid==(timer==26), "SCORE acc read");
            if(step.acc_read.valid){
                expect(step.acc_read.is_constant, "SCORE zero old L");
                expect(step.acc_read.rmw, "SCORE L RMW");
            }
            expect(
                step.conflict_free==(timer==EXP2_END),
                "SCORE conflict-free cycle"
            );
        }

        inst.acc.zero = false;
        expect(
            fsa::make_execution_plan_step(inst, 18).acc_ctrl.valid &&
            fsa::make_execution_plan_step(inst, 18).acc_ctrl.bits.cmd==
                fsa::AccumulatorCmd::EXP_S1,
            "SCORE nonzero path EXP_S1"
        );
        expect(
            fsa::make_execution_plan_step(inst, 19).acc_ctrl.valid &&
            fsa::make_execution_plan_step(inst, 19).acc_ctrl.bits.cmd==
                fsa::AccumulatorCmd::EXP_S2,
            "SCORE nonzero path EXP_S2"
        );
    }

    void checkValueAndNorm(){
        fsa::MatrixInstruction value =
            instruction(fsa::MxFunc::ATTENTION_VALUE_COMPUTE);
        value.spad.addr = 8;
        value.spad.stride = 1;
        value.acc.addr = 1;
        value.acc.stride = 1;
        for(unsigned timer=0; timer<12; ++timer){
            const fsa::ExecutionPlanStep step =
                fsa::make_execution_plan_step(value, timer);
            expect(step.sp_read.valid==(timer<4), "VALUE sp read");
            expect(
                step.acc_read.valid==inRange(timer, 7, 4),
                "VALUE acc read"
            );
            if(step.acc_read.valid){
                expect(
                    step.acc_read.addr.to_uint()==1+(timer-7),
                    "VALUE O address"
                );
            }
            expect(
                step.acc_ctrl.valid==inRange(timer, 8, 4),
                "VALUE accumulator table"
            );
            expect(
                step.semaphore_release==(timer==3),
                "VALUE semaphore release"
            );
            expect(
                step.conflict_free==(timer==6),
                "VALUE conflict-free"
            );
            for(int row=0; row<fsa::SA_ROWS; ++row){
                expect(
                    step.pe_ctrl[row].valid==flowDown(timer, row, 1, 4),
                    "VALUE PE wave"
                );
            }
        }

        const fsa::MatrixInstruction scale =
            instruction(fsa::MxFunc::ATTENTION_LSE_NORM_SCALE);
        for(unsigned timer=0; timer<17; ++timer){
            const fsa::ExecutionPlanStep step =
                fsa::make_execution_plan_step(scale, timer);
            expect(step.acc_read.valid==(timer==0), "NORM_SCALE acc read");
            expect(step.acc_ctrl.valid==(timer>=1), "NORM_SCALE ctrl valid");
            if(timer==1){
                expect(
                    step.acc_ctrl.bits.cmd==fsa::AccumulatorCmd::SET_SCALE,
                    "NORM_SCALE SET_SCALE"
                );
            }else if(timer>=2){
                expect(
                    step.acc_ctrl.bits.cmd==fsa::AccumulatorCmd::RECIPROCAL,
                    "NORM_SCALE RECIPROCAL"
                );
            }
            expect(
                step.semaphore_release==(timer==16),
                "NORM_SCALE semaphore release"
            );
            expect(
                step.conflict_free==(timer==16),
                "NORM_SCALE conflict-free"
            );
        }

        const fsa::MatrixInstruction norm =
            instruction(fsa::MxFunc::ATTENTION_LSE_NORM);
        for(unsigned timer=0; timer<5; ++timer){
            const fsa::ExecutionPlanStep step =
                fsa::make_execution_plan_step(norm, timer);
            expect(step.cmp_ctrl.valid==(timer==0), "NORM CMP RESET");
            expect(step.acc_read.valid==(timer<4), "NORM O read");
            expect(
                step.acc_ctrl.valid==inRange(timer, 1, 4),
                "NORM ACC table"
            );
            expect(
                step.semaphore_release==(timer==4),
                "NORM semaphore release"
            );
            expect(step.conflict_free==(timer==4), "NORM conflict-free");
        }
    }

}  // namespace

int main(){
    checkLengths();
    checkLoad();
    checkScore();
    checkValueAndNorm();

    if(failure_count!=0){
        std::cerr << "[FAIL] test_execution_plan: " << failure_count
                  << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "[PASS] test_execution_plan: five Chisel schedules match"
              << std::endl;
    return 0;
}
