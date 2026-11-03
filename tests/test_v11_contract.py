from pathlib import Path
R=Path(__file__).resolve().parents[1]
def text(path):return (R/path).read_text(encoding='utf-8-sig')
def test_tracking_is_compile_time_excluded():
    mk=text('jni/Android.mk');assert 'RN_VARIANT ?= official' in mk
    assert 'ifeq ($(RN_VARIANT),test)\nLOCAL_SRC_FILES += src/rebootnova/tracking_manager.cpp\nendif' in mk
    assert '#if RN_WITH_TRACKING' in text('jni/src/rebootnova/ui.cpp')
def test_variant_build_cannot_pick_legacy_binary():
    b=text('tools/build.ps1');assert '$bin = Join-Path $variantLibs' in b
    assert 'NDK_OUT=$variantObj NDK_LIBS_OUT=$variantLibs' in b

def test_input_reader_is_readonly_and_selected():
    s=text('jni/src/rebootnova/converter_input.cpp')
    assert 'O_RDONLY|O_NONBLOCK|O_CLOEXEC' in s and 'EVIOCGRAB' not in s
    assert 'selector.empty() || n.description.selector==selector' in s
    assert 'std::chrono::seconds(2)' in s and 'nodes.size()>=32' in s
    assert 'ABS_MT_SLOT' in s
    assert 'ioctl(fd,EVIOCGABS(ABS_X)' not in s

def test_input_drop_disconnect_and_generation_guard():
    s=text('jni/src/rebootnova/converter_input.cpp');h=text('jni/include/rebootnova/converter_input.h')
    for t in ['SYN_DROPPED','EVIOCGKEY','POLLHUP','nodes.erase','generation_.fetch_add(1)']:assert t in s
    assert '(h>>32)==generation_.load()' in h

def test_boxed_hud_press_activates_aim():
    s=text('jni/src/rebootnova/feature_engine.cpp')
    assert 'HasPhysicalTouchInNormalizedRegion' in s
    assert 'Boxed HUD buttons: any contact inside the fire/ADS region is a press.' in s

def test_touch_sparse_names_and_slot_bounds():
    s=text('third_party/surface_imgui/jni/src/Android_touch/TouchHelperA.cpp')
    assert 'eventCount' not in s
    assert s.count('closedir(dir)')>=1
    assert 'latest < 0 || latest >= 10' in s
    assert 'ie.type == EV_SYN && ie.code == SYN_REPORT' in s
    assert 'SYN_MT_REPORT' in s
    assert 'device.type_b' in s

def test_display_maintenance_off_render_thread():
    s=text('jni/src/rebootnova/main.cpp');loop=s.split('while (running && !stop_requested)',1)[1]
    assert 'GetDisplayInfo()' not in loop and 'ProcessMirrorDisplay()' not in loop
    assert 'display_monitor.stop();' in loop
    assert '/system/bin/timeout 2 /system/bin/dumpsys display' in text('third_party/surface_imgui/jni/include/native_surface/ANativeWindowCreator.h')

def test_scaled_render_preserves_logical_coordinates():
    s=text('third_party/surface_imgui/jni/src/Android_Graphics/OpenGLGraphics.cpp')
    assert 'io.DisplaySize={m_Width,m_Height}' in s
    assert 'io.DisplayFramebufferScale={w/m_Width,h/m_Height}' in s
    assert 'graphics->NewFrame(false)' in text('jni/src/rebootnova/main.cpp')

def test_refresh_vote_is_optional_and_dynamic():
    s=text('third_party/surface_imgui/jni/src/Android_Graphics/OpenGLGraphics.cpp')
    assert 'dlsym(RTLD_DEFAULT,"ANativeWindow_setFrameRate")' in s
    assert 'refresh_vote?static_cast<float>(fps):0.0F' in s
    assert 'eglSwapInterval(m_EglDisplay, 0)' in s
    assert 'eglSwapInterval(m_EglDisplay, 1)' not in s
    assert 'refresh_vote?int8_t(1):int8_t(0)' in s

def test_touch_does_not_grab_usb_converter_nodes():
    s=text('third_party/surface_imgui/jni/src/Android_touch/TouchHelperA.cpp')
    assert 'BUS_USB' in s and 'BUS_BLUETOOTH' in s
    assert 'converter_bus' in s
    assert 'ioctl(fd, EVIOCGRAB, GRAB)' in s

def test_diagnostics_separate_submit_swap_and_compositor():
    s=text('jni/include/rebootnova/platform_performance.h')
    for t in ['prepare_ms','submit_ms','swap_ms','display_query_ms','mirror_ms','buffer_width','refresh_status']:assert t in s
