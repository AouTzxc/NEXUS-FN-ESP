#pragma once
#include "rebootnova/platform_performance.h"

#include "rebootnova/game_reader.h"
#include "rebootnova/settings.h"
#include "rebootnova/build_variant.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>


struct ImDrawList;

namespace rebootnova {

class FeatureEngine {
public:
    FeatureEngine(ProcessMemory& memory, GameReader& reader, Settings& settings);
    ~FeatureEngine();

    void tick(float screen_width, float screen_height);
    const performance::Stats& performance_stats() const {return performance_meter_.stats;}
    void report_performance(double frame_seconds,double draw_ms,double present_ms){
        performance_meter_.add(frame_seconds,last_captured_,last_data_ms_,last_feature_ms_,draw_ms,present_ms);
    }
    void draw(ImDrawList* draw_list, float screen_width, float screen_height) const;
    const FrameSnapshot& frame() const { return frame_; }
    float projection_width() const { return projection_width_; }
    float projection_height() const { return projection_height_; }
    float projection_focal_basis() const { return projection_focal_basis_; }
    bool projection_vertical_fov() const { return projection_vertical_fov_; }
    std::size_t offscreen_indicator_count() const { return offscreen_indicator_count_; }
    std::size_t pose_box_count() const { return pose_box_count_; }
    std::size_t fixed_box_count() const { return fixed_box_count_; }
    std::size_t draw_entity_count() const { return draw_entity_count_; }
    std::size_t draw_projection_count() const { return draw_projection_count_; }
    void report_platform_performance(const performance::PlatformStats& p) { platform_stats_ = p; }
    const performance::PlatformStats& platform_stats() const { return platform_stats_; }
    void set_profile(const OffsetProfile& profile) { reader_.set_profile(profile); }

private:
    struct RenderBoxState {
        Vec2 head{};
        Vec2 bottom{};
        float width{};
        std::chrono::steady_clock::time_point last_seen{};
        bool initialized{};
    };

    GameReader& reader_;
    Settings& settings_;
    FrameSnapshot frame_{};
    performance::SampleGate capture_gate_{};
    performance::Meter performance_meter_{};
    bool last_captured_{};
    double last_data_ms_{},last_feature_ms_{};
    std::thread capture_worker_{};
    std::mutex capture_mutex_{};
    std::condition_variable capture_cv_{};
    std::atomic_bool capture_stop_{false};
    FrameSnapshot published_frame_{};
    double published_data_ms_{};
    bool published_is_capture_{};
    std::uint64_t published_generation_{};
    std::uint64_t seen_generation_{};
    std::atomic_bool urgent_capture_{false};
    void capture_loop();

    float projection_width_{};
    float projection_height_{};
    float projection_focal_basis_{};
    bool projection_vertical_fov_{};
    mutable std::size_t offscreen_indicator_count_{};
    mutable std::size_t pose_box_count_{};
    mutable std::size_t fixed_box_count_{};
    mutable std::size_t draw_entity_count_{};
    mutable std::size_t draw_projection_count_{};
    performance::PlatformStats platform_stats_{};
    mutable std::unordered_map<std::uintptr_t, RenderBoxState> render_boxes_;
};

}  // namespace rebootnova
