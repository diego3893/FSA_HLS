#include <iostream>
#include <string>

#include "fsa/matrix_engine_controller.hpp"

namespace{

    int failures = 0;

    void expect(const bool condition, const std::string& message){
        if(!condition){
            std::cerr << "[FAIL] " << message << std::endl;
            ++failures;
        }
    }

}  // namespace

int main(){
    fsa::MatrixEngineControllerState current{};
    fsa::reset_matrix_engine_controller_state(current);

    fsa::MatrixInstruction instruction{};
    instruction.header.func = fsa::MxFunc::ATTENTION_SCORE_COMPUTE;
    instruction.header.releaseValid = true;
    instruction.header.semId = 7;
    instruction.header.releaseSemValue = 3;

    fsa::MatrixEngineControllerInput accept{};
    accept.instruction_valid = true;
    accept.instruction = instruction;

    fsa::MatrixEngineControllerState next{};
    fsa::MatrixEngineControllerOutput output{};
    fsa::matrix_engine_controller_step(current, next, accept, output);
    expect(output.instruction_ready, "idle controller did not report ready");
    expect(output.instruction_accepted, "score instruction was not accepted");
    expect(output.busy, "accepted instruction did not set busy");
    current = next;

    unsigned plan_steps = 0;
    unsigned release_count = 0;
    bool done = false;
    for(int guard=0; guard<64 && !done; ++guard){
        fsa::MatrixEngineControllerState step_next{};
        fsa::MatrixEngineControllerOutput step_output{};
        fsa::matrix_engine_controller_step(
            current,
            step_next,
            fsa::MatrixEngineControllerInput{},
            step_output
        );
        if(step_output.plan.valid){
            ++plan_steps;
        }
        if(step_output.sem_release.valid){
            ++release_count;
            expect(
                step_output.sem_release.bits.id==7,
                "semaphore id mismatch"
            );
            expect(
                step_output.sem_release.bits.value==3,
                "semaphore value mismatch"
            );
        }
        done = step_output.instruction_done;
        current = step_next;
    }

    expect(done, "controller did not finish score instruction");
    expect(
        plan_steps==fsa::execution_plan_length(instruction),
        "controller emitted the wrong number of plan steps"
    );
    expect(release_count==1, "controller emitted wrong semaphore count");
    expect(!current.busy, "controller remained busy after done");

    if(failures!=0){
        return 1;
    }
    std::cout << "[PASS] test_matrix_engine_controller: single-FSM score plan"
              << std::endl;
    return 0;
}
