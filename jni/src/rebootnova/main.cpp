#include "rebootnova/config_store.h"
#include "rebootnova/display_monitor.h"
#include "rebootnova/feature_engine.h"
#include "rebootnova/game_reader.h"
#include "rebootnova/offset_profile.h"
#include "rebootnova/process_memory.h"
#include "rebootnova/settings.h"
#include "rebootnova/ui.h"
#include "rebootnova/runtime_log.h"

#include "AndroidImgui.h"
#include "OpenGLGraphics.h"
#include "TouchHelperA.h"
#include "my_imgui.h"
#include "native_surface/ANativeWindowCreator.h"
#include "imgui.h"

#include <android/native_window.h>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <sys/prctl.h>
#include <thread>
#include <vector>
#include <zlib.h>

extern "C" {
extern const unsigned char rebootnova_embedded_font_gz[];
extern const unsigned int rebootnova_embedded_font_gz_size;
}

namespace {
volatile std::sig_atomic_t stop_requested=0;
void request_stop(int){stop_requested=1;}

std::vector<unsigned char> inflate_gzip(const unsigned char* data, std::size_t size) {
    if (!data || size == 0) return {};
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return {};
    std::vector<unsigned char> out(512 * 1024);
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    int rc = Z_OK;
    while (rc == Z_OK) {
        if (stream.total_out + 64 >= out.size()) out.resize(out.size() * 2);
        stream.next_out = out.data() + stream.total_out;
        stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
        rc = inflate(&stream, Z_NO_FLUSH);
    }
    const auto produced = static_cast<std::size_t>(stream.total_out);
    inflateEnd(&stream);
    if (rc != Z_STREAM_END) return {};
    out.resize(produced);
    return out;
}

struct Options {
    std::string profile;
    std::string config{"/data/local/tmp/HX-MOD.ini"};
    std::string font;
    rebootnova::Language language{rebootnova::Language::Chinese};
    bool probe{};
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string{}; };
        if (arg == "--profile") options.profile = next();
        else if (arg == "--config") options.config = next();
        else if (arg == "--font") options.font = next();
        else if (arg == "--lang" && next() == "en") options.language = rebootnova::Language::English;
        else if (arg == "--probe") options.probe = true;
        else if (arg == "--help") {
            std::puts("REBOOT-ESP --profile FILE --config FILE --font TTF --lang zh|en");
            std::exit(0);
        }
    }
    return options;
}

bool make_surface(std::unique_ptr<AndroidImgui>& graphics, ANativeWindow*& window,
                  int width, int height, bool record_visible, const std::string& font_path,
                  std::vector<unsigned char>& embedded_font) {
    rebootnova::runtime::set_stage("surface.create");
    window = android::ANativeWindowCreator::Create("REBOOT-ESP", width, height, !record_visible);
    if (!window) { rebootnova::runtime::log("surface create returned null"); return false; }
    const int surface_width = ANativeWindow_getWidth(window) > 0 ? ANativeWindow_getWidth(window) : width;
    const int surface_height = ANativeWindow_getHeight(window) > 0 ? ANativeWindow_getHeight(window) : height;
    graphics = std::make_unique<OpenGLGraphics>();
    rebootnova::runtime::set_stage("graphics.init");
    if (!graphics->Init_Render(window, static_cast<float>(surface_width), static_cast<float>(surface_height))) {
        rebootnova::runtime::log("graphics Init_Render failed");
        graphics.reset();
        android::ANativeWindowCreator::Destroy(window);
        window = nullptr;
        return false;
    }
    rebootnova::runtime::set_stage("font.load");
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    bool bundled_font_loaded = false;
    if (!embedded_font.empty())
        bundled_font_loaded = ImGui::My_Android_LoadFontMemory(
            embedded_font.data(), static_cast<int>(embedded_font.size()), 30.0F, false);
    if (!bundled_font_loaded && !font_path.empty())
        bundled_font_loaded = ImGui::My_Android_LoadFontFile(font_path.c_str(), 30.0F);
    bool atlas_built = bundled_font_loaded && atlas->Build();
    bool font_ready = atlas_built && atlas->Fonts.Size > 0 && atlas->Fonts[0] != nullptr
                      && atlas->Fonts[0]->IsLoaded() && atlas->Fonts[0]->ContainerAtlas == atlas;
    if (font_ready) io.FontDefault = ImGui::SystemFont;
    if (!font_ready) {
        atlas->Clear();
        io.FontDefault = nullptr;
        ImGui::SystemFont = nullptr;
        const bool system_font_loaded = ImGui::My_Android_LoadSystemFont(30.0F);
        atlas_built = system_font_loaded && atlas->Build();
        font_ready = atlas_built && atlas->Fonts.Size > 0 && atlas->Fonts[0] != nullptr
                     && atlas->Fonts[0]->IsLoaded() && atlas->Fonts[0]->ContainerAtlas == atlas;
        if (font_ready) io.FontDefault = ImGui::SystemFont;
    }
    if (!font_ready) {
        atlas->Clear();
        io.FontDefault = nullptr;
        ImGui::SystemFont = nullptr;
        ImFont* fallback = atlas->AddFontDefault();
        atlas_built = fallback != nullptr && atlas->Build();
        font_ready = atlas_built && fallback != nullptr && fallback->IsLoaded()
                     && fallback->ContainerAtlas == atlas;
        if (font_ready) io.FontDefault = fallback;
    }
    if (!font_ready) {
        rebootnova::runtime::log("font atlas initialization failed");
        graphics->Shutdown();
        graphics.reset();
        android::ANativeWindowCreator::Destroy(window);
        window = nullptr;
        return false;
    }
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = false;
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(1.75F);
    style.FramePadding = {14.0F, 12.0F};
    style.ItemSpacing = {12.0F, 12.0F};
    style.ScrollbarSize = 52.0F;
    style.ScrollbarRounding = 12.0F;
    style.TouchExtraPadding = {14.0F, 14.0F};
    style.GrabMinSize = 36.0F;
    rebootnova::runtime::set_stage("surface.ready");
    return true;
}

void destroy_surface(std::unique_ptr<AndroidImgui>& graphics, ANativeWindow*& window) {
    if (graphics) { graphics->Shutdown(); graphics.reset(); }
    if (window) { android::ANativeWindowCreator::Destroy(window); window = nullptr; }
}

}  // namespace

int main(int argc, char** argv) {
    prctl(PR_SET_NAME, "REBOOT-ESP", 0, 0, 0);
    rebootnova::runtime::initialize();
    rebootnova::runtime::install_crash_handlers();
    rebootnova::runtime::set_stage("main.options");
    const Options options = parse_options(argc, argv);

    rebootnova::runtime::set_stage("config.load");
    rebootnova::Settings settings;
    settings.language = options.language;
    rebootnova::ConfigStore config(options.config);
    std::string config_error;
    config.load(settings, &config_error);
    // The box-only delivery must not inherit a disabled box flag from an older build.
    settings.boxes = true;
    settings.max_distance_m = 1000.0F;

    rebootnova::OffsetProfile profile = rebootnova::OffsetProfile::builtin_59343437();
    std::string profile_error;
    if (!options.profile.empty())
        profile.load_ini(options.profile, &profile_error);  // Missing external profile leaves the verified built-in profile active.
    rebootnova::ProcessMemory memory;
    rebootnova::GameReader reader(memory, profile);
    rebootnova::FeatureEngine features(memory, reader, settings);
    rebootnova::Ui ui(settings, features, config, profile, options.profile);

    rebootnova::runtime::set_stage("display.query.initial");
    auto display = android::ANativeWindowCreator::GetDisplayInfo();
    int width = display.width;
    int height = display.height;
    if (width <= 0 || height <= 0) { rebootnova::runtime::log("display information unavailable"); return 2; }

    static std::vector<unsigned char> embedded_font = inflate_gzip(
        rebootnova_embedded_font_gz, rebootnova_embedded_font_gz_size);
    std::unique_ptr<AndroidImgui> graphics;
    ANativeWindow* window = nullptr;
    if (!make_surface(graphics, window, width, height, settings.record_visible, options.font, embedded_font)) return 3;
    int projection_width = ANativeWindow_getWidth(window);
    int projection_height = ANativeWindow_getHeight(window);
    if (projection_width <= 0 || projection_height <= 0) {
        projection_width = width;
        projection_height = height;
    }
    rebootnova::runtime::set_stage("touch.init.uinput");
    bool touch_ready = Touch::Init({static_cast<float>(projection_width), static_cast<float>(projection_height)}, false);
    if (!touch_ready) rebootnova::runtime::log("touch initialization unavailable");
    Touch::setOrientation(display.orientation);

    std::signal(SIGTERM, request_stop);
    std::signal(SIGINT, request_stop);
    rebootnova::DisplayMonitor display_monitor(display);
    bool running = true;
    std::uint32_t last_orientation = display.orientation;
    auto next_display_poll=std::chrono::steady_clock::now();
    auto previous_frame_start=next_display_poll;
    while (running && !stop_requested) {
        rebootnova::runtime::set_stage("frame.begin");
        const auto frame_start = std::chrono::steady_clock::now();
        const double frame_seconds=std::chrono::duration<double>(frame_start-previous_frame_start).count();
        previous_frame_start=frame_start;
        if(frame_start>=next_display_poll){
            const auto latest=display_monitor.snapshot();
            if(latest.width>0&&latest.height>0)display=latest;
            next_display_poll=frame_start+std::chrono::milliseconds(250);
        }
        if (display.width > 0 && display.height > 0
            && (display.width != width || display.height != height)) {
            Touch::Close();
            destroy_surface(graphics, window);
            width = display.width;
            height = display.height;
            if (!make_surface(graphics, window, width, height, settings.record_visible, options.font, embedded_font)) break;
            projection_width = ANativeWindow_getWidth(window);
            projection_height = ANativeWindow_getHeight(window);
            if (projection_width <= 0 || projection_height <= 0) {
                projection_width = width;
                projection_height = height;
            }
            touch_ready = Touch::Init({static_cast<float>(projection_width), static_cast<float>(projection_height)}, false);
            Touch::setOrientation(display.orientation);
            last_orientation = display.orientation;
            if (!touch_ready) rebootnova::runtime::log("touch reinitialization unavailable");
            continue;
        }
        if (last_orientation != static_cast<std::uint32_t>(display.orientation)) {
            last_orientation = display.orientation;
            Touch::setOrientation(display.orientation);
        }
        display_monitor.mirror(!settings.record_visible);
        auto* gl=static_cast<OpenGLGraphics*>(graphics.get());
        gl->Configure(settings.overlay_scale_percent,settings.fps_cap,settings.overlay_refresh_vote);
        Touch::UpdateImGui();
        graphics->NewFrame(false); // OpenGL supplies logical size + real framebuffer ratio
        const auto prepared=std::chrono::steady_clock::now();
        rebootnova::runtime::set_stage("frame.imgui");
        features.tick(static_cast<float>(projection_width), static_cast<float>(projection_height));
        const auto draw_start=std::chrono::steady_clock::now();
        features.draw(ImGui::GetBackgroundDrawList(), static_cast<float>(projection_width),
                      static_cast<float>(projection_height));
        ui.render(&running, touch_ready, graphics->RenderName);
        const auto present_start=std::chrono::steady_clock::now();
        graphics->EndFrame();
        const auto present_end=std::chrono::steady_clock::now();
        features.report_platform_performance({
            std::chrono::duration<double,std::milli>(prepared-frame_start).count(),
            gl->submit_ms,gl->swap_ms,display_monitor.query_ms(),display_monitor.mirror_ms(),
            projection_width,projection_height,gl->buffer_width,gl->buffer_height,
            gl->geometry_status,gl->refresh_status});
        features.report_performance(frame_seconds,
            std::chrono::duration<double,std::milli>(present_start-draw_start).count(),
            std::chrono::duration<double,std::milli>(present_end-present_start).count());
        rebootnova::runtime::set_stage("frame.presented");
        if (options.probe) {
            const auto& frame = features.frame();
            const auto& diag = frame.entity_diagnostics;
            rebootnova::runtime::log(
                "probe pid=%d world=0x%llX camera=%d actors=%d/%d fp_player=%d "
                "fp_world=%d fp_unknown=%d geometry=%d bones=%d accepted=%d/%d/%d "
                "entities=%zu stage=%s error=%s",
                frame.pid, static_cast<unsigned long long>(frame.local.world),
                frame.local.camera.valid ? 1 : 0, diag.actors_scanned,
                diag.actor_count, diag.player_fingerprint, diag.world_fingerprint,
                diag.fingerprint_unknown, diag.geometry_ok, diag.bones_ok,
                diag.accepted_players, diag.accepted_fallback, diag.accepted_world,
                frame.entities.size(), diag.stage.c_str(), frame.error.c_str());
            running = false;
        }

        if (ui.wants_surface_recreate()) {
            ui.clear_surface_recreate();
            destroy_surface(graphics, window);
            if (!make_surface(graphics, window, width, height, settings.record_visible, options.font, embedded_font)) break;
            projection_width = ANativeWindow_getWidth(window);
            projection_height = ANativeWindow_getHeight(window);
            if (projection_width <= 0 || projection_height <= 0) {
                projection_width = width;
                projection_height = height;
            }
        }
        const int fps = std::clamp(settings.fps_cap, 20, 144);
        const auto target = std::chrono::microseconds(1000000 / fps);
        const auto elapsed = std::chrono::steady_clock::now() - frame_start;
        if (elapsed < target) std::this_thread::sleep_for(target - elapsed);
    }

    display_monitor.stop();
    Touch::Close();
    destroy_surface(graphics, window);
    rebootnova::runtime::shutdown();
    return 0;
}
