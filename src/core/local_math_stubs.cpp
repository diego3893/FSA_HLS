/**
 * 普通g++本地回归使用的HLS math替身。Vitis CSim/综合不定义
 * FSA_LOCAL_MATH_STUBS，因此不会把这些宿主实现带入硬件路径。
 */
#ifdef FSA_LOCAL_MATH_STUBS

#include <cmath>

namespace hls{
    float fabs(const float value){ return std::fabs(value); }
    float fma(const float a, const float b, const float c){
        return std::fma(a, b, c);
    }
    float trunc(const float value){ return std::trunc(value); }
    float ldexp(const float value, const int exponent){
        return std::ldexp(value, exponent);
    }
    float sqrt(const float value){ return std::sqrt(value); }
}

#endif
