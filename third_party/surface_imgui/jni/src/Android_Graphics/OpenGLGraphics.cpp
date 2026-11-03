#include <GLES3/gl3.h>
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <android/native_window.h>
#include "OpenGLGraphics.h"
#include "rebootnova/render_geometry.h"
#include "imgui_impl_opengl3.h"


#if !defined(EGL_OPENGL_ES3_BIT)
    // 如果 EGL_OPENGL_ES3_BIT 未定义，则编译这部分代码
    #define EGL_OPENGL_ES3_BIT                0x00000040
#endif

bool OpenGLGraphics::Create() {
    const EGLint egl_attributes[] = {EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
                                     EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLE_BUFFERS, 0,
        EGL_SAMPLES, 0,
                                     EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE,
                                     EGL_WINDOW_BIT, EGL_NONE};

    m_EglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_EglDisplay == EGL_NO_DISPLAY || eglInitialize(m_EglDisplay, nullptr, nullptr) != EGL_TRUE)
        return false;
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        Cleanup();
        return false;
    }
    EGLint num_configs = 0;
    if (eglChooseConfig(m_EglDisplay, egl_attributes, nullptr, 0, &num_configs) != EGL_TRUE || num_configs <= 0) {
        Cleanup();
        return false;
    }
    EGLConfig egl_config{};
    if (eglChooseConfig(m_EglDisplay, egl_attributes, &egl_config, 1, &num_configs) != EGL_TRUE || num_configs <= 0) {
        Cleanup();
        return false;
    }
    EGLint egl_format{};
    if (eglGetConfigAttrib(m_EglDisplay, egl_config, EGL_NATIVE_VISUAL_ID, &egl_format) != EGL_TRUE
        || ANativeWindow_setBuffersGeometry(m_Window, 0, 0, egl_format) != 0) {
        Cleanup();
        return false;
    }

    const EGLint egl_context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    m_EglContext = eglCreateContext(m_EglDisplay, egl_config, EGL_NO_CONTEXT,
                                    egl_context_attributes);
    if (m_EglContext == EGL_NO_CONTEXT) {
        Cleanup();
        return false;
    }
    m_EglSurface = eglCreateWindowSurface(m_EglDisplay, egl_config, m_Window, nullptr);
    if (m_EglSurface == EGL_NO_SURFACE
        || eglMakeCurrent(m_EglDisplay, m_EglSurface, m_EglSurface, m_EglContext) != EGL_TRUE) {
        Cleanup();
        return false;
    }
    format_ = egl_format;
    // vsync=1 binds Overlay to SurfaceFlinger. On 8 Gen3/Gen5 tablets that
    // often means 10/30 Hz even when CPU/GPU are idle. Elite devices ignore
    // this and still present at the voted rate. Interval 0 lets the CPU cap
    // in main.cpp own cadence.
    eglSwapInterval(m_EglDisplay, 0);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    return true;
}

void OpenGLGraphics::Configure(int percent, int fps, bool refresh_vote) {
    percent=std::clamp(percent,50,100);fps=std::clamp(fps,20,144);
    if(percent!=scale_) {
        const int w=rebootnova::performance::buffer_extent(m_Width,percent);
        const int h=rebootnova::performance::buffer_extent(m_Height,percent);
        geometry_status=ANativeWindow_setBuffersGeometry(m_Window,w,h,format_);
        // Avoid retrying a rejected geometry request every frame.
        scale_=percent;
    }
    if(fps!=requested_fps_||refresh_vote!=requested_vote_) {
        // Optional API30 symbol: old devices remain loadable (min SDK unchanged).
        // compatibility=1: ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE.
        // compatibility=0 is "default" and lets SurfaceFlinger pick 10/30 Hz on
        // some tablet displays even when the vote itself succeeds.
        using Vote=int(*)(ANativeWindow*,float,int8_t);
        static Vote vote=reinterpret_cast<Vote>(dlsym(RTLD_DEFAULT,"ANativeWindow_setFrameRate"));
        refresh_status=vote ? vote(m_Window,refresh_vote?static_cast<float>(fps):0.0F,refresh_vote?int8_t(1):int8_t(0)) : -38;
        requested_fps_=fps;requested_vote_=refresh_vote;
        eglSwapInterval(m_EglDisplay, 0);
    }
}

void OpenGLGraphics::Setup() {
    ImGui_ImplOpenGL3_Init("#version 300 es");
}

void OpenGLGraphics::PrepareFrame(bool resize) {
    ImGui_ImplOpenGL3_NewFrame();
    EGLint w=0,h=0;
    if(eglQuerySurface(m_EglDisplay,m_EglSurface,EGL_WIDTH,&w)!=EGL_TRUE || w<=0)w=static_cast<int>(m_Width);
    if(eglQuerySurface(m_EglDisplay,m_EglSurface,EGL_HEIGHT,&h)!=EGL_TRUE || h<=0)h=static_cast<int>(m_Height);
    buffer_width=w;buffer_height=h;
    auto& io=ImGui::GetIO();
    io.DisplaySize={m_Width,m_Height}; // logical pixels remain unchanged for projection and touch
    io.DisplayFramebufferScale={w/m_Width,h/m_Height};
}

void OpenGLGraphics::Render(ImDrawData *drawData) {
    const auto begin=std::chrono::steady_clock::now();
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
    const auto submitted=std::chrono::steady_clock::now();
    eglSwapBuffers(m_EglDisplay, m_EglSurface);
    const auto swapped=std::chrono::steady_clock::now();
    submit_ms=std::chrono::duration<double,std::milli>(submitted-begin).count();
    swap_ms=std::chrono::duration<double,std::milli>(swapped-submitted).count();
}

void OpenGLGraphics::PrepareShutdown() {
    ImGui_ImplOpenGL3_Shutdown();
}

void OpenGLGraphics::Cleanup() {
    if (m_EglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_EglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_EglContext != EGL_NO_CONTEXT) eglDestroyContext(m_EglDisplay, m_EglContext);
        if (m_EglSurface != EGL_NO_SURFACE) eglDestroySurface(m_EglDisplay, m_EglSurface);
        eglTerminate(m_EglDisplay);
    }
    m_EglDisplay = EGL_NO_DISPLAY;
    m_EglSurface = EGL_NO_SURFACE;
    m_EglContext = EGL_NO_CONTEXT;
}

BaseTexData *OpenGLGraphics::LoadTexture(BaseTexData *tex, void *pixel_data) {
    auto tex_data = new OpenglTextureData();
    tex_data->Width = tex->Width;
    tex_data->Height = tex->Height;
    tex_data->Channels = tex->Channels;

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_data->Width, tex_data->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);
    tex_data->DS = (void *) (intptr_t) textureId;
    return tex_data;
}

void OpenGLGraphics::RemoveTexture(BaseTexData *tex) {
    auto tex_data = (OpenglTextureData *) tex;
    auto textureId = (GLuint) (intptr_t) tex_data->DS;
    glDeleteTextures(1, &textureId);
    delete tex_data;
}
