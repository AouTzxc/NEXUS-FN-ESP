#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace rebootnova::ring_style {
inline constexpr float default_width=1.2F;
inline constexpr std::array<float,4> default_color{100.F/255.F,235.F/255.F,199.F/255.F,100.F/255.F};
inline constexpr std::array<float,4> default_line_color{100.F/255.F,235.F/255.F,199.F/255.F,225.F/255.F};
inline constexpr std::array<float,4> default_lead_color{1.F,199.F/255.F,95.F/255.F,210.F/255.F};
inline float width(float value,float fallback=default_width){return std::clamp(std::isfinite(value)?value:fallback,.5F,8.F);}
inline float channel(float value,std::size_t index,const std::array<float,4>& fallback=default_color){
    return std::clamp(std::isfinite(value)?value:fallback[index%4],0.F,1.F);
}
}
