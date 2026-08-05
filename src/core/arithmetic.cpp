#include "fsa/arithmetic.hpp"

#include <cmath>
#include <limits>

namespace fsa{

acc_t peMac(const elem_t in_a, const elem_t in_b, const acc_t in_c){
    return (acc_t)in_a*(acc_t)in_b + in_c;
}

acc_t accUnit(const acc_t in_a, const acc_t in_b, const acc_t in_c){
    return in_a*in_b + in_c;
}

CmpUnitOutput accCmp(const acc_t in_a, const acc_t in_b){
    CmpUnitOutput output{};
    output.out_max = (in_a>=in_b) ? in_a : in_b;
    output.out_diff = in_a-in_b;
    return output;
}

elem_t cvtAtoE(const acc_t a){
    // TODO: 目前的实现不好，在舍入上可能有问题
    return (elem_t)a;
}

acc_t viewEasA(const elem_t e){
    // TODO: 需要改为按位操作
    return (acc_t)e;
}

elem_t viewAasE(const acc_t a){
    // TODO: 需要改为按位操作
    return (elem_t)a;
}

elem_t peExp2Pwl(const elem_t x, const elem_t slope, const acc_t intercept){
    return (elem_t)((acc_t)slope*(acc_t)x + intercept);
}

acc_t accExp2Pwl(const acc_t x){
    // TODO：应使用PWL计算，可能需要构造查找表
    return std::exp2(x);
}

acc_t reciprocal(const acc_t value){
    return (acc_t)1.0F/value;
}

elem_t elemZero(){
    return (elem_t)0.0F;
}

elem_t elemOne(){
    return (elem_t)1.0F;
}

acc_t accZero(){
    return (acc_t)0.0F;
}

acc_t accMinimum(){
    return -std::numeric_limits<acc_t>::infinity();
}

acc_t attentionScale(const int dk){
    // TODO: 直接写成一个constexpr会不会更好？
    if(dk <= 0){
        return accZero();
    }
    const acc_t log2_e = std::log2(std::exp((acc_t)1.0F));
    return log2_e / std::sqrt((acc_t)dk);
}
}  // namespace fsa