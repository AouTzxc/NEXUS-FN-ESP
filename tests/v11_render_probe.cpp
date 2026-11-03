#include "OpenGLGraphics.h"
#include "native_surface/ANativeWindowCreator.h"
#include "rebootnova/render_geometry.h"
#include "imgui.h"
#include <android/native_window.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <cstdio>
#include <thread>
int main(){
    const auto display=android::ANativeWindowCreator::GetDisplayInfo();
    if(display.width<=0||display.height<=0)return 2;
    auto* window=android::ANativeWindowCreator::Create("RN-V11-render-probe",display.width,display.height,false);
    if(!window)return 3;
    OpenGLGraphics gl;
    const int w=ANativeWindow_getWidth(window),h=ANativeWindow_getHeight(window);
    if(!gl.Init_Render(window,w,h)){android::ANativeWindowCreator::Destroy(window);return 4;}
    ImGui::GetIO().Fonts->AddFontDefault();int failed=0;
    for(int scale:{100,75,50,100}){
        gl.Configure(scale,60,true);
        for(int i=0;i<15;++i){
            gl.NewFrame(false);
            auto* draw=ImGui::GetBackgroundDrawList();
            draw->AddText({20,80},IM_COL32(255,255,255,200),"V11 independent rendering check");
            draw->AddCircle({w*.5F,h*.5F},80,IM_COL32(0,220,160,180),64,2);
            gl.EndFrame();
            if(i==14){
                const auto size=ImGui::GetIO().DisplaySize;const auto fb=ImGui::GetIO().DisplayFramebufferScale;
                const bool ok=gl.geometry_status==0&&gl.buffer_width==rebootnova::performance::buffer_extent(w,scale)&&gl.buffer_height==rebootnova::performance::buffer_extent(h,scale)&&size.x==w&&size.y==h;
                const auto error=glGetError();if(!ok||error!=GL_NO_ERROR)++failed;
                std::printf("RENDER scale=%d logical=%dx%d buffer=%dx%d ratio=%.3f,%.3f geometry=%d refresh=%d gl_error=%u submit_ms=%.2f swap_ms=%.2f ok=%d\n",scale,w,h,gl.buffer_width,gl.buffer_height,fb.x,fb.y,gl.geometry_status,gl.refresh_status,error,gl.submit_ms,gl.swap_ms,ok?1:0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    gl.Shutdown();android::ANativeWindowCreator::Destroy(window);
    std::printf("V11_RENDER_TESTS passed=%d failed=%d SURFACE_DESTROYED\n",4-failed,failed);return failed?1:0;
}
