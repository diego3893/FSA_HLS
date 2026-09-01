/**
 * @file test_stream_pe.cpp
 * @brief 验证流式PE的局部状态、单FMA opcode复用和连续token。
 */

#include <cassert>
#include <cmath>
#include <iostream>

#include "fsa/arithmetic.hpp"
#include "fsa/stream_pe.hpp"

int main(){
    fsa::StreamPeState state{};
    fsa::reset_stream_pe_state(state);

    fsa::StreamPeToken token{};
    token.valid = true;
    token.op = fsa::StreamPeOp::LOAD_Q;
    token.horizontal = (fsa::elem_t)2.0F;
    fsa::StreamPeOutput output = fsa::stream_pe_step(state, token);
    assert(output.register_written);
    assert((float)state.reg==2.0F);

    token.op = fsa::StreamPeOp::QK_MAC;
    token.horizontal = (fsa::elem_t)3.0F;
    token.vertical = (fsa::acc_t)4.0F;
    output = fsa::stream_pe_step(state, token);
    assert(std::fabs(output.down.vertical-10.0F)<1.0e-6F);
    assert((float)state.reg==2.0F);

    token.op = fsa::StreamPeOp::LOAD_SCORE;
    token.horizontal = (fsa::elem_t)0.5F;
    token.vertical = 0.0F;
    output = fsa::stream_pe_step(state, token);
    assert((float)state.reg==0.5F);

    token.op = fsa::StreamPeOp::SUB_MAX;
    token.horizontal = fsa::elemOne();
    token.vertical = -1.0F;
    output = fsa::stream_pe_step(state, token);
    assert((float)state.reg==-0.5F);

    token.op = fsa::StreamPeOp::SCALE_SCORE;
    token.horizontal = fsa::elemOne();
    token.vertical = 0.0F;
    output = fsa::stream_pe_step(state, token);
    assert((float)state.reg==-0.5F);

    int commits = 0;
    for(int piece=0; piece<fsa::exp2PWLPieces; ++piece){
        token.op = fsa::StreamPeOp::EXP2_PWL;
        token.horizontal = (fsa::elem_t)(
            piece==0 ? 0.664062500F :
            piece==1 ? 0.608886719F :
            piece==2 ? 0.558105469F :
            piece==3 ? 0.512207031F :
            piece==4 ? 0.469482422F :
            piece==5 ? 0.430419922F :
            piece==6 ? 0.394775391F : 0.362060547F
        );
        token.vertical = fsa::exp2PWLIntercept(
            (fsa::exp2_counter_t)piece
        );
        token.last = piece==fsa::exp2PWLPieces-1;
        output = fsa::stream_pe_step(state, token);
        commits += output.register_written ? 1 : 0;
    }
    assert(commits==1);
    assert(std::fabs((float)state.reg-std::exp2(-0.5F))<5.0e-3F);

    token.op = fsa::StreamPeOp::PV_MAC;
    token.horizontal = (fsa::elem_t)4.0F;
    token.vertical = (fsa::acc_t)1.0F;
    output = fsa::stream_pe_step(state, token);
    assert(std::fabs(
        output.down.vertical-(1.0F+4.0F*std::exp2(-0.5F))
    )<2.0e-2F);

    std::cout << "[PASS] test_stream_pe: local state and opcode-shared FMA"
              << std::endl;
    return 0;
}
