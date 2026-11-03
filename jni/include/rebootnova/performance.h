#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace rebootnova::performance {
struct Preset { int fps, entity_hz, scan_kb, detail_m, circle_segments; bool text_shadow; };
inline Preset preset(int index) {
    if(index==0)return {60,30,256,120,32,false};
    if(index==2)return {120,60,1024,500,96,true};
    return {90,45,512,220,64,true};
}
// Does not catch up missed samples in a burst. Rate changes take effect immediately.
struct SampleGate {
    double last{-1},next{-1};
    int rate{};
    bool due(double now,int hz) const {
        hz=std::clamp(hz,20,120);
        // A small deadline tolerance avoids halving 60 Hz sampling on a 60 Hz
        // display due to sub-millisecond vsync jitter. Retain fractional phase.
        return !std::isfinite(now)||last<0||now<last||hz!=rate||now+.0005>=next;
    }
    void sampled(double now,int hz){
        hz=std::clamp(hz,20,120);const double period=1.0/hz;
        if(last<0||now<last||hz!=rate||!std::isfinite(now)||now-next>period)
            next=now+period;
        else next+=period;
        last=now;rate=hz;
    }
};
inline bool reusable(double age,bool same_session) {
    return same_session && std::isfinite(age) && age>=0 && age<=.25;
}
inline std::size_t take_budget(std::size_t& remaining,std::size_t request) {
    const auto taken=std::min(remaining,request);remaining-=taken;return taken;
}
struct Stats {
    double render_hz{},capture_hz{},frame_ms{},peak_frame_ms{},data_ms{},feature_ms{},draw_ms{},present_ms{};
};
struct Meter {
    Stats stats{};
    double elapsed{},max_frame{},data{},feature{},draw{},present{};
    int frames{},captures{};
    void add(double seconds,bool captured,double d,double f,double g,double p) {
        if(!std::isfinite(seconds)||seconds<=0||seconds>10)return;
        elapsed+=seconds;++frames;captures+=captured?1:0;
        max_frame=std::max(max_frame,seconds*1000);
        data+=d;feature+=f;draw+=g;present+=p;
        if(elapsed>=.5){
            stats={frames/elapsed,captures/elapsed,elapsed*1000/frames,max_frame,
                   data/frames,feature/frames,draw/frames,present/frames};
            elapsed=max_frame=data=feature=draw=present=0;frames=captures=0;
        }
    }
};
}
