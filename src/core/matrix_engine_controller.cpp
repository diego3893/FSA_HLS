#include "fsa/matrix_engine_controller.hpp"

namespace fsa{

    void reset_matrix_engine_controller_state(
        MatrixEngineControllerState& state
    ){
        #pragma HLS INLINE
        state = MatrixEngineControllerState{};
    }

    void matrix_engine_controller_step(
        const MatrixEngineControllerState& current,
        MatrixEngineControllerState& next,
        const MatrixEngineControllerInput& input,
        MatrixEngineControllerOutput& output
    ){
        #pragma HLS INLINE

        next = current;
        output = MatrixEngineControllerOutput{};
        output.instruction_ready = !current.busy;
        output.busy = current.busy;

        if(!current.busy){
            if(input.instruction_valid){
                const unsigned length =
                    execution_plan_length(input.instruction);
                output.instruction_accepted = length!=0;
                if(length!=0){
                    next.busy = true;
                    next.instruction = input.instruction;
                    next.timer = 0;
                    next.length = length;
                    output.busy = true;
                }
            }
            return;
        }

        output.plan = make_execution_plan_step(
            current.instruction,
            current.timer.to_uint()
        );

        if(output.plan.semaphore_release &&
                current.instruction.header.releaseValid){
            Semaphore release{};
            release.id = current.instruction.header.semId;
            release.value = current.instruction.header.releaseSemValue;
            output.sem_release = make_valid(release);
        }

        if(current.timer+1>=current.length){
            next.busy = false;
            next.timer = 0;
            next.length = 0;
            output.busy = false;
            output.instruction_done = true;
        }else{
            next.timer = current.timer+1;
        }
    }

}  // namespace fsa
