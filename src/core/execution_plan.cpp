#include "fsa/execution_plan.hpp"

#include "fsa/arithmetic.hpp"

namespace fsa{
    namespace{

        constexpr unsigned SCORE_EXP2_START = 2*SA_ROWS+4;
        constexpr unsigned SCORE_EXP2_END = SCORE_EXP2_START+exp2PWLPieces-1;

        const elem_t EXP2_SLOPES[exp2PWLPieces] = {
            (elem_t)0.664062500F,
            (elem_t)0.608886719F,
            (elem_t)0.558105469F,
            (elem_t)0.512207031F,
            (elem_t)0.469482422F,
            (elem_t)0.430419922F,
            (elem_t)0.394775391F,
            (elem_t)0.362060547F
        };

        static_assert(exp2PWLPieces==8,
                    "当前ExecutionPlan常量表对应8段FP16 exp2 PWL");

        bool inRange(
            const unsigned timer,
            const unsigned start,
            const unsigned repeat
        ){
            return timer>=start && timer<start+repeat;
        }

        /// @brief PE上行控制波前生成
        bool flowUp(
            const unsigned timer,
            const int row,
            const unsigned start,
            const unsigned repeat
        ){
            const unsigned lane_start = start+(SA_ROWS-1-row);
            return inRange(timer, lane_start, repeat);
        }

        /// @brief PE下行控制波前生成
        bool flowDown(
            const unsigned timer,
            const int row,
            const unsigned start,
            const unsigned repeat
        ){
            const unsigned lane_start = start+row;
            return inRange(timer, lane_start, repeat);
        }

        /// @brief 计算sram地址
        sram_address_t steppedAddress(
            const sram_address_t base,
            const sram_stride_t stride,
            const unsigned index
        ){
            const ap_int<SRAM_ADDRESS_WIDTH+2> address =
                (ap_int<SRAM_ADDRESS_WIDTH+2>)base+
                (ap_int<SRAM_ADDRESS_WIDTH+2>)stride*
                (ap_int<SRAM_ADDRESS_WIDTH+2>)index;
            return (sram_address_t)address;
        }

        void setSpRead(
            ExecutionPlanStep& step,
            const MatrixInstruction& instruction,
            const unsigned index
        ){
            step.sp_read.valid = true;
            step.sp_read.addr = steppedAddress(
                instruction.spad.addr,
                instruction.spad.stride,
                index
            );
            step.sp_read.rev_sram_out = instruction.spad.revInput;
            step.sp_read.delay_sram_out = instruction.spad.delayOutput;
            step.sp_read.rev_delayer_out = instruction.spad.revOutput;
        }

        void setSpConstant(
            ExecutionPlanStep& step,
            const elem_t value
        ){
            step.sp_read.valid = true;
            step.sp_read.is_constant = true;
            step.sp_read.delay_sram_out = true;
            step.sp_constant_value = value;
        }

        void setAccRead(
            ExecutionPlanStep& step,
            const MatrixInstruction& instruction,
            const unsigned index,
            const bool rmw
        ){
            step.acc_read.valid = true;
            step.acc_read.is_constant = instruction.acc.zero;
            step.acc_read.addr = steppedAddress(
                instruction.acc.addr,
                instruction.acc.stride,
                index
            );
            step.acc_read.rmw = rmw;
            step.acc_constant_value = (acc_t)0.0F;
        }

        void setCmp(
            ExecutionPlanStep& step,
            const MatrixInstruction& instruction,
            const CmpControlCmd command,
            const unsigned key_index
        ){
            CmpControl control{};
            control.cmd = command;

            unsigned mask_prefix = 0;
            if(command==CmpControlCmd::UPDATE && instruction.acc.causal){
                const unsigned global_key =
                    instruction.acc.keyBase+key_index;
                if(global_key>instruction.acc.queryBase){
                    mask_prefix = global_key-instruction.acc.queryBase;
                }
            }

            // 矩形阵列中只有activeRows个K/V token有效。把无效token
            // 对所有query列屏蔽，使其softmax概率严格为0。
            if(command==CmpControlCmd::UPDATE &&
                    key_index>=instruction.acc.activeRows){
                mask_prefix = SA_COLS;
            }
            if(mask_prefix>(unsigned)SA_COLS){
                mask_prefix = SA_COLS;
            }
            control.causalCounter = (std::uint8_t)mask_prefix;
            step.cmp_ctrl = make_valid(control);
        }

        void setAccumulator(
            ExecutionPlanStep& step,
            const AccumulatorCmd command
        ){
            AccumulatorControl control{};
            control.cmd = command;
            step.acc_ctrl = make_valid(control);
        }

        void finishStep(
            ExecutionPlanStep& step,
            const unsigned timer,
            const unsigned length,
            const unsigned release_cycle,
            const unsigned conflict_free_cycle
        ){
            step.valid = timer<length;
            step.first = step.valid && timer==0;
            step.last = step.valid && timer+1==length;
            step.semaphore_release = step.valid && timer==release_cycle;
            step.conflict_free = step.valid && timer==conflict_free_cycle;
        }

    }  // namespace

    unsigned execution_plan_length(const MatrixInstruction& instruction){
        switch(instruction.header.func){
            case MxFunc::LOAD_STATIONARY:
                return SA_COLS+1;
            case MxFunc::ATTENTION_SCORE_COMPUTE:
                return SCORE_EXP2_END+SA_ROWS+SA_COLS+1;
            case MxFunc::ATTENTION_VALUE_COMPUTE:
                return SA_ROWS+SA_COLS+SA_ROWS;
            case MxFunc::ATTENTION_LSE_NORM_SCALE:
                return 2+reciprocalLatency;
            case MxFunc::ATTENTION_LSE_NORM:
                return SA_ROWS+1;
        }
        return 0;
    }

    ExecutionPlanStep make_execution_plan_step(
        const MatrixInstruction& instruction,
        const unsigned timer
    ){
        ExecutionPlanStep step{};
        const unsigned length = execution_plan_length(instruction);
        if(timer>=length){
            return step;
        }

        switch(instruction.header.func){
            case MxFunc::LOAD_STATIONARY:{
                if(timer<(unsigned)SA_COLS){
                    setSpRead(step, instruction, timer);
                }
                if(inRange(timer, 1, SA_COLS)){
                    PECtrl control{};
                    control.load_reg_li = true;
                    for(int row=0; row<SA_ROWS; ++row){
                        #pragma HLS UNROLL
                        step.pe_ctrl[row] = make_valid(control);
                    }
                }
                finishStep(
                    step,
                    timer,
                    length,
                    SA_COLS-1,
                    SA_COLS-1
                );
                break;
            }

            case MxFunc::ATTENTION_SCORE_COMPUTE:{
                if(timer<(unsigned)SA_ROWS){ // 读K
                    setSpRead(step, instruction, timer);
                }else if(timer==2*SA_ROWS+1){ // 广播1，1*S-newMax
                    setSpConstant(step, (elem_t)1.0F);
                }else if(timer==2*SA_ROWS+2){ // 广播缩放因子
                    setSpConstant(step, elemAttentionScale());
                }else if(inRange(
                    timer,
                    SCORE_EXP2_START-1,
                    exp2PWLPieces
                )){ // exp2PWL，Sp读取斜率
                    setSpConstant(
                        step,
                        EXP2_SLOPES[timer-(SCORE_EXP2_START-1)]
                    );
                }else if(timer==SCORE_EXP2_END){ // 广播1，rowsum(P)
                    setSpConstant(step, (elem_t)1.0F);
                }

                if(inRange(timer, SA_ROWS+1, SA_ROWS)){ // 求newMax
                    setCmp(
                        step,
                        instruction,
                        CmpControlCmd::UPDATE,
                        timer-(SA_ROWS+1)
                    );
                }else if(timer==2*SA_ROWS+1){ // 广播-newMax
                    setCmp(step, instruction, CmpControlCmd::PROP_MAX, 0);
                }else if(timer==2*SA_ROWS+2){ // 计算diff
                    setCmp(
                        step,
                        instruction,
                        CmpControlCmd::PROP_MAX_DIFF,
                        0
                    );
                }else if(inRange(
                    timer,
                    SCORE_EXP2_START-1,
                    exp2PWLPieces
                )){ // exp2PWL，CMP广播截距
                    setCmp(
                        step,
                        instruction,
                        CmpControlCmd::PROP_EXP2_INTERCEPTS,
                        timer-(SCORE_EXP2_START-1)
                    );
                }else if(timer==SCORE_EXP2_END){ // 广播0常量，rowsum(P)
                    setCmp(step, instruction, CmpControlCmd::PROP_ZERO, 0);
                }

                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    PECtrl control{};
                    control.mac =
                        flowUp(timer, row, 1, SA_ROWS) ||
                        flowDown(timer, row, SCORE_EXP2_END+1, 1);
                    control.acc_ui =
                        flowDown(timer, row, 2*SA_ROWS+2, 1) ||
                        flowDown(
                            timer,
                            row,
                            SCORE_EXP2_START,
                            exp2PWLPieces
                        ) ||
                        flowDown(timer, row, SCORE_EXP2_END+1, 1);
                    control.load_reg_ui = timer==2*SA_ROWS+1;
                    control.flow_lr =
                        flowUp(timer, row, 1, SA_ROWS) ||
                        flowDown(timer, row, 2*SA_ROWS+2, 1) ||
                        flowDown(timer, row, 2*SA_ROWS+3, 1) ||
                        flowDown(
                            timer,
                            row,
                            SCORE_EXP2_START,
                            exp2PWLPieces
                        ) ||
                        flowDown(timer, row, SCORE_EXP2_END+1, 1);
                    control.flow_ud =
                        flowDown(timer, row, SA_ROWS+1, SA_ROWS) ||
                        flowDown(timer, row, 2*SA_ROWS+2, 1) ||
                        flowDown(timer, row, 2*SA_ROWS+3, 1) ||
                        flowDown(
                            timer,
                            row,
                            SCORE_EXP2_START,
                            exp2PWLPieces
                        );
                    control.flow_du =
                        flowUp(timer, row, SA_ROWS+4, SA_ROWS);
                    control.update_reg =
                        flowDown(timer, row, 2*SA_ROWS+2, 1) ||
                        flowDown(timer, row, 2*SA_ROWS+3, 1);
                    control.exp2 = flowDown(
                        timer,
                        row,
                        SCORE_EXP2_START,
                        exp2PWLPieces
                    );

                    const bool valid = control.mac || control.acc_ui ||
                        control.load_reg_ui || control.flow_lr ||
                        control.flow_ud || control.flow_du ||
                        control.update_reg || control.exp2;
                    if(valid){
                        step.pe_ctrl[row] = make_valid(control);
                    }
                }

                // 旧值非0才计算缩放因子alpha
                if(!instruction.acc.zero && timer==3*SA_ROWS+SA_COLS+2){
                    setAccumulator(step, AccumulatorCmd::EXP_S1);
                }else if(!instruction.acc.zero &&
                        timer==3*SA_ROWS+SA_COLS+3){
                    setAccumulator(step, AccumulatorCmd::EXP_S2);
                }else if(timer==SCORE_EXP2_END+SA_ROWS+SA_COLS){ // 计算新L
                    setAccumulator(step, AccumulatorCmd::ACC_SA);
                }

                if(timer==SCORE_EXP2_END+SA_ROWS+SA_COLS-1){ // 读旧L
                    setAccRead(step, instruction, 0, true);
                }

                finishStep(
                    step,
                    timer,
                    length,
                    SA_ROWS-1,
                    SCORE_EXP2_END
                );
                break;
            }

            case MxFunc::ATTENTION_VALUE_COMPUTE:{
                if(timer<(unsigned)SA_ROWS){ // 读V
                    setSpRead(step, instruction, timer);
                }

                for(int row=0; row<SA_ROWS; ++row){ // 计算PV
                    #pragma HLS UNROLL
                    if(flowDown(timer, row, 1, SA_ROWS)){
                        PECtrl control{};
                        control.mac = true;
                        control.acc_ui = true;
                        control.flow_lr = true;
                        step.pe_ctrl[row] = make_valid(control);
                    }
                }

                const unsigned read_start = SA_ROWS+SA_COLS-1;
                const unsigned acc_start = SA_ROWS+SA_COLS;
                if(inRange(timer, read_start, SA_ROWS)){ // 读旧O
                    setAccRead(step, instruction, timer-read_start, true);
                }
                if(inRange(timer, acc_start, SA_ROWS)){ // 算新O
                    setAccumulator(step, AccumulatorCmd::ACC_SA);
                }

                finishStep(
                    step,
                    timer,
                    length,
                    SA_ROWS-1,
                    2*SA_ROWS-2
                );
                break;
            }

            case MxFunc::ATTENTION_LSE_NORM_SCALE:{
                if(timer==0){ // 读L
                    setAccRead(step, instruction, 0, false);
                }else if(timer==1){ // L存进scale
                    setAccumulator(step, AccumulatorCmd::SET_SCALE);
                }else{ // 倒数计算
                    setAccumulator(step, AccumulatorCmd::RECIPROCAL);
                }
                finishStep(step, timer, length, length-1, length-1);
                break;
            }

            case MxFunc::ATTENTION_LSE_NORM:{
                if(timer==0){ // 清空CMP
                    setCmp(step, instruction, CmpControlCmd::RESET, 0);
                }
                if(timer<(unsigned)SA_ROWS){ // 读O
                    setAccRead(step, instruction, timer, true);
                }
                if(inRange(timer, 1, SA_ROWS)){ // 计算O/L
                    setAccumulator(step, AccumulatorCmd::ACC);
                }
                finishStep(step, timer, length, SA_ROWS, SA_ROWS);
                break;
            }
        }

        return step;
    }

}  // namespace fsa
