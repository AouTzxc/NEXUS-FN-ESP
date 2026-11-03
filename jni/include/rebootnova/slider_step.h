#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace rebootnova::slider_step {
inline double precision(const char* format) {
    if(format && std::strstr(format,"%.2f"))return .01;
    if(format && std::strstr(format,"%.1f"))return .1;
    return 1.;
}
inline float adjust(float value,float minimum,float maximum,double step,int direction) {
    if(!std::isfinite(minimum)||!std::isfinite(maximum)||minimum>maximum)return value;
    const double current=std::clamp(std::isfinite(value)?double(value):double(minimum),double(minimum),double(maximum));
    if(!std::isfinite(step)||step<=0||direction==0)return float(current);
    const double next=std::round((current+(direction>0?step:-step))/step)*step;
    return float(std::clamp(next,double(minimum),double(maximum)));
}
inline int adjust(int value,int minimum,int maximum,int direction) {
    if(minimum>maximum)return value;
    const std::int64_t current=std::clamp(value,minimum,maximum);
    const std::int64_t next=current+(direction>0?1:direction<0?-1:0);
    return int(std::clamp(next,std::int64_t(minimum),std::int64_t(maximum)));
}
}
