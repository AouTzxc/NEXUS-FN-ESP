#pragma once
#include <algorithm>
namespace rebootnova::performance {
inline int buffer_extent(float logical, int percent) {
    // Window creation validates the positive logical extent; cap avoids overflow on corrupt input.
    if (!(logical>0 && logical<=32768)) return 1;
    return std::max(1,static_cast<int>(logical*std::clamp(percent,50,100)/100));
}
}
