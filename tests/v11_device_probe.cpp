#include "rebootnova/build_variant.h"
#include "rebootnova/config_store.h"
#include "rebootnova/converter_input.h"
#include "rebootnova/converter_keys.h"
#include "rebootnova/render_geometry.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
using namespace rebootnova;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    int passed=0;ConfigStore store(argv[1]);Settings s;
    assert(!s.aim_converter_enabled&&!s.aim_converter_mapped_touch&&s.overlay_scale_percent==100);++passed;
    s.aim_converter_enabled=true;s.aim_converter_mapped_touch=true;s.aim_converter_fire_key=30;s.aim_converter_ads_key=31;
    s.aim_converter_device="3:1:2:Test keyboard";s.overlay_scale_percent=75;s.overlay_refresh_vote=false;
    s.tracking_enabled=true;s.tracking_switch_delay_ms=1234;
    assert(store.save(s));Settings loaded;assert(store.load(loaded));
    assert(loaded.aim_converter_enabled&&loaded.aim_converter_mapped_touch&&loaded.aim_converter_fire_key==30&&loaded.aim_converter_ads_key==31&&loaded.aim_converter_device==s.aim_converter_device&&loaded.overlay_scale_percent==75&&!loaded.overlay_refresh_vote);++passed;
    {std::ifstream f(argv[1]);std::stringstream text;text<<f.rdbuf();
#if RN_WITH_TRACKING
    assert(text.str().find("tracking_enabled=true")!=std::string::npos&&loaded.tracking_enabled&&loaded.tracking_switch_delay_ms==1234);
#else
    assert(text.str().find("tracking_")==std::string::npos&&!loaded.tracking_enabled);
#endif
    }++passed;
    {std::ofstream f(argv[1]);f<<"version=31\naim_converter_fire_key=-100\naim_converter_ads_key=9000\noverlay_scale_percent=2\ntracking_enabled=true\n";}
    loaded=Settings{};assert(store.load(loaded));assert(loaded.aim_converter_fire_key==0&&loaded.aim_converter_ads_key==767&&loaded.overlay_scale_percent==50&&!loaded.aim_converter_enabled);++passed;
    {std::ofstream f(argv[1]);f<<"version=30\nfps_cap=90\n";}
    assert(store.load(loaded));assert(!loaded.aim_converter_enabled&&!loaded.aim_converter_mapped_touch&&loaded.overlay_scale_percent==100&&loaded.overlay_refresh_vote&&loaded.aim_converter_device.empty());++passed;
    using converter::Keys;Keys k;assert(k.held(272,273)==0);k.key(272,1);assert(k.held(272,273)==1);k.key(273,1);assert(k.held(272,273)==3);k.key(272,0);assert(k.held(272,273)==2);++passed;
    k.clear();k.key(272,2);assert(k.held(272,273)==1);k.key(-1,1);k.key(768,1);k.key(272,7);assert(k.held(272,273)==1&&!k.down(-1)&&!k.down(768));++passed;
    k.dropped=true;k.clear();k.key(272,1);assert(k.held(272,273)==0);k.dropped=false;k.key(273,1);assert(k.held(272,273)==2);++passed;
    using performance::buffer_extent;
    assert(buffer_extent(3000,75)==2250&&buffer_extent(2120,75)==1590&&buffer_extent(3392,50)==1696&&buffer_extent(2400,50)==1200);++passed;
    assert(buffer_extent(3000,1)==1500&&buffer_extent(3000,999)==3000&&buffer_extent(0,75)==1&&buffer_extent(std::numeric_limits<float>::quiet_NaN(),75)==1);++passed;
    for(int size:{2120,3000,3392,2400})for(int pct:{50,75,100}) {
        const float scale=static_cast<float>(buffer_extent(static_cast<float>(size),pct))/size;
        const float logical=size*.37F;assert(std::abs((logical*scale)/scale-logical)<.001F);
    }++passed;
    {ConverterInput input;assert(input.held()==0);input.configure(true,272,273,"");
     std::this_thread::sleep_for(std::chrono::milliseconds(150));
     // Empty selector never activates unless exactly one HID exists (rooted converter).
     if(input.devices()!=1)assert(input.held()==0);
     input.configure(false,272,273,"");assert(input.held()==0);
     std::this_thread::sleep_for(std::chrono::milliseconds(40));assert(input.devices()==0);
    }++passed;
    std::remove(argv[1]);std::printf("V11_DEVICE_TESTS variant=%s passed=%d failed=0\n",build_variant::name,passed);return 0;
}
