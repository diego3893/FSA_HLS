#include "fsa/arithmetic.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

#include <utils/x_hls_utils.h>

int main(){
    const float reference = static_cast<float>(
        1.4426950408889634074/std::sqrt((double)fsa::SA_ROWS)
    );
    const fsa::acc_t acc_value = fsa::attentionScale();
    const fsa::elem_t elem_value = fsa::elemAttentionScale();

    const fp_struct<fsa::acc_t> acc_view(acc_value);
    const fp_struct<fsa::acc_t> reference_view(reference);
    const fp_struct<fsa::elem_t> elem_view(elem_value);

    if(acc_view.data()!=reference_view.data()){
        std::cerr << "[FAIL] FP32 attentionScale位模式错误\n";
        return 1;
    }
    if(elem_view.data()!=(ap_uint<16>)fsa::ATTENTION_SCALE_ELEM_BITS){
        std::cerr << "[FAIL] FP16 attentionScale位模式错误\n";
        return 1;
    }

    const fsa::elem_t reference_elem = (fsa::elem_t)reference;
    const fp_struct<fsa::elem_t> reference_elem_view(reference_elem);
    if(elem_view.data()!=reference_elem_view.data()){
        std::cerr << "[FAIL] FP16 attentionScale不符合RNE舍入\n";
        return 1;
    }

    std::cout << "[PASS] test_attention_scale: SA_ROWS="
              << fsa::SA_ROWS
              << ", FP32=" << (float)acc_value
              << ", FP16=" << (float)elem_value << '\n';
    return 0;
}
