#include "fsa/arithmetic.hpp"

#include <cmath>
#include <limits>

namespace fsa{

namespace{
// TODO: 这是agent跑出来的数据，需要人为确认，并且做精度调整
const acc_t EXP2_PWL_INTERCEPTS[exp2PWLPieces] = {
    (acc_t)1.0,
    (acc_t)0.993111670,
    (acc_t)0.980478406,
    (acc_t)0.963101327,
    (acc_t)0.941854775,
    (acc_t)0.917500854,
    (acc_t)0.890701711,
    (acc_t)0.862030923
};
static_assert(exp2PWLPieces==8, "PWL段数改变，重新计算slope/intercept");

/**
 * @brief 确保PWL有效，并查找段号
 * 
 * @param intercept 截距值
 * @param index 分段编号
 * @return true 找到对应分段
 * @return false 未找到对应分段
 */
bool findExp2PWLInterceptIndex(const acc_t intercept, exp2_counter_t& index){
    for(exp2_counter_t piece=0; piece<(exp2_counter_t)exp2PWLPieces; ++piece){
        if(intercept == EXP2_PWL_INTERCEPTS[piece]){
            index = piece;
            return true;
        }
    }
    return false;
}

/**
 * @brief 求x小数部分对应的段号
 * 
 * @param x 幂指数
 * @return exp2_counter_t 段号
 */
exp2_counter_t exp2PWLPieceForX(const elem_t x){
    const acc_t x_acc = (acc_t)x;
    const acc_t integer_part = std::trunc(x_acc);
    const acc_t fractional_magnitude = std::fabs(x_acc - integer_part);
    // 8段均分，因此可以直接映射
    exp2_counter_t piece = (exp2_counter_t)(fractional_magnitude * (acc_t)exp2PWLPieces);
    // 边界保护
    if(piece >= (exp2_counter_t)exp2PWLPieces){
        piece = (exp2_counter_t)(exp2PWLPieces - 1);
    }
    return piece;
}

}  // namespace

acc_t peMac(const elem_t in_a, const elem_t in_b, const acc_t in_c){
    // TODO: 应为融合乘加，此处精度有误
    return (acc_t)in_a*(acc_t)in_b + in_c;
}

PeMacUnitOutput peMacUnit(const elem_t in_a, const elem_t in_b, 
                          const acc_t in_c, const bool in_exp2){
    PeMacUnitOutput output{};
    if(!in_exp2){
        output.out_accType = peMac(in_a, in_b, in_c);
        output.out_elemType = cvtAtoE(output.out_accType);
        output.out_exp2 = false;
        return output;
    }

    // TODO: 是否需要从一个raw做两次转换？
    output.out_elemType = peExp2PWL(in_a, in_b, in_c);
    output.out_accType = (acc_t)output.out_elemType;

    exp2_counter_t intercept_index = 0;
    const bool intercept_index_valid = findExp2PWLInterceptIndex(in_c, intercept_index);
    output.out_exp2 = intercept_index_valid && intercept_index == exp2PWLPieceForX(in_a);
    return output;
}

acc_t accUnit(const acc_t in_a, const acc_t in_b, const acc_t in_c){
    return in_a*in_b + in_c;
}

CmpUnitOutput accCmp(const acc_t in_a, const acc_t in_b){
    CmpUnitOutput output{};
    // TODO: 正确实现为浮点减法之后看符号位
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

elem_t peExp2PWL(const elem_t x, const elem_t slope, const acc_t intercept){
    const acc_t x_acc = (acc_t)x;
    const int integer_part = (int)std::trunc(x_acc);
    const acc_t fractional_part = x_acc - (acc_t)integer_part;
    const acc_t fractional_exp2 = (acc_t)slope*fractional_part + intercept;
    return (elem_t)std::ldexp(fractional_exp2, integer_part);
}

acc_t exp2PWLIntercept(const exp2_counter_t index){
    const exp2_counter_t bounded_index = index % (exp2_counter_t)exp2PWLPieces;
    return EXP2_PWL_INTERCEPTS[bounded_index];
}

acc_t accExp2PWL(const acc_t x){
    // TODO：应使用PWL计算，可能需要构造查找表
    return std::exp2(x);
}

acc_t reciprocal(const acc_t value){
    // TODO: 应为多周期除法器，带有busy状态
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