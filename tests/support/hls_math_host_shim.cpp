/**
 * @file hls_math_host_shim.cpp
 * @brief 未安装Vitis运行库时供普通主机编译器链接的最小数学函数垫片
 *
 * 该文件只用于本地纯C++测试，不加入任何HLS工程。服务器上的Vitis C仿真
 * 继续使用工具自带的hls::数学实现。
 */

#include <cmath>

namespace hls{

float fabs(const float value){
    return std::fabs(value);
}

float fma(const float a, const float b, const float c){
    return std::fma(a, b, c);
}

float trunc(const float value){
    return std::trunc(value);
}

float ldexp(const float value, const int exponent){
    return std::ldexp(value, exponent);
}

}  // namespace hls
