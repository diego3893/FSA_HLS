/**
 * @file test_fsa_stream_vs_legacy.cpp
 * @brief 在tile边界比较流式核与current/next参考核。
 */

#include <cassert>
#include <cmath>
#include <iostream>

#include "fsa/hls/fsa_core_request_top.hpp"
#include "fsa/hls/fsa_stream_request_top.hpp"

namespace{

    fsa::FsaCoreRequestInput makeRequest(
        const int block,
        const bool initialize,
        const bool finalize
    ){
        fsa::FsaCoreRequestInput input{};
        input.request_valid = true;
        input.initialize = initialize;
        input.finalize = finalize;
        input.active_keys = fsa::SA_COLS;
        input.key_base = block*fsa::SA_COLS;

        for(int query=0; query<fsa::SA_COLS; ++query){
            for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                input.q[query][feature] = (fsa::elem_t)(
                    (float)(((query+2*feature)%5)-2)*0.25F
                );
            }
        }
        for(int key=0; key<fsa::SA_COLS; ++key){
            for(int feature=0; feature<fsa::SA_ROWS; ++feature){
                input.k[key][feature] = (fsa::elem_t)(
                    (float)(((2*(block*fsa::SA_COLS+key)+feature)%7)-3)*
                    0.25F
                );
                input.v[key][feature] = (fsa::elem_t)(
                    (float)((((block+1)*key+feature)%6)-2)*0.5F
                );
            }
        }
        return input;
    }

}  // namespace

int main(){
    fsa::FsaCoreRequestInput reset{};
    reset.reset = true;
    fsa::FsaCoreRequestOutput discard{};
    fsa_core_request_top(reset, discard);

    fsa::FsaCoreRequestOutput legacy{};
    for(int block=0; block<2; ++block){
        const fsa::FsaCoreRequestInput request = makeRequest(
            block, block==0, block==1
        );
        fsa_core_request_top(request, legacy);
        assert(legacy.request_done && !legacy.protocol_error);
    }

    fsa_stream_request_top(reset, discard);
    fsa::FsaCoreRequestOutput streaming{};
    for(int block=0; block<2; ++block){
        const fsa::FsaCoreRequestInput request = makeRequest(
            block, block==0, block==1
        );
        fsa_stream_request_top(request, streaming);
        assert(streaming.request_done && !streaming.protocol_error);
    }

    for(int query=0; query<fsa::SA_COLS; ++query){
        assert(std::fabs(streaming.l[query]-legacy.l[query])<=0.12F);
        for(int feature=0; feature<fsa::SA_ROWS; ++feature){
            const float error = std::fabs(
                streaming.o[query][feature]-legacy.o[query][feature]
            );
            if(error>0.12F){
                std::cerr << "query=" << query
                          << ", feature=" << feature
                          << ", stream=" << streaming.o[query][feature]
                          << ", legacy=" << legacy.o[query][feature]
                          << std::endl;
            }
            assert(error<=0.12F);
        }
    }

    std::cout << "[PASS] test_fsa_stream_vs_legacy: tile-boundary match"
              << std::endl;
    return 0;
}
