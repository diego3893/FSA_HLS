/**
 * @file test_fsa_stream_request_top.cpp
 * @brief 流式请求HLS顶层的最小事务测试。
 */

#include <cassert>
#include <iostream>

#include "fsa/hls/fsa_stream_request_top.hpp"

int main(){
    fsa::FsaCoreRequestInput input{};
    input.reset = true;
    input.request_valid = true;
    input.initialize = true;
    input.finalize = true;
    input.active_keys = fsa::SA_COLS;

    for(int query=0; query<fsa::SA_COLS; ++query){
        for(int feature=0; feature<fsa::SA_ROWS; ++feature){
            input.q[query][feature] =
                (fsa::elem_t)(query==feature ? 1.0F : 0.0F);
        }
    }
    for(int key=0; key<fsa::SA_COLS; ++key){
        for(int feature=0; feature<fsa::SA_ROWS; ++feature){
            input.k[key][feature] =
                (fsa::elem_t)(key==feature ? 1.0F : 0.0F);
            input.v[key][feature] =
                (fsa::elem_t)(key==feature ? 1.0F : 0.0F);
        }
    }

    fsa::FsaCoreRequestOutput output{};
    fsa_stream_request_top(input, output);
    assert(output.request_done);
    assert(!output.protocol_error);
    assert(output.normalized);
    std::cout << "[PASS] test_fsa_stream_request_top" << std::endl;
    return 0;
}
