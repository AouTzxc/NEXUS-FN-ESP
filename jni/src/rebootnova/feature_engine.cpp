#include "rebootnova/feature_engine.h"

#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace rebootnova {
namespace {
constexpr float kRootToFeet = 75.0F;
constexpr float kRootToHead = 85.0F;
constexpr float kMinimumBoxHeight = 6.0F;
constexpr auto kMissingEntityHold = std::chrono::milliseconds(350);
constexpr auto kRenderStateExpiry = std::chrono::milliseconds(650);
}

FeatureEngine::FeatureEngine(ProcessMemory&, GameReader& reader, Settings& settings)
    : reader_(reader), settings_(settings) {
    capture_worker_ = std::thread(&FeatureEngine::capture_loop, this);
}

FeatureEngine::~FeatureEngine() {
    capture_stop_.store(true, std::memory_order_release);
    capture_cv_.notify_all();
    if (capture_worker_.joinable()) capture_worker_.join();
}

void FeatureEngine::capture_loop() {
    while (!capture_stop_.load(std::memory_order_acquire)) {
        const auto data_start = std::chrono::steady_clock::now();
        const double now = std::chrono::duration<double>(data_start.time_since_epoch()).count();
        const int hz = settings_.entity_refresh_hz;
        const bool due = capture_gate_.due(now, hz) ||
                         urgent_capture_.exchange(false, std::memory_order_acq_rel);
        if (due) {
            FrameSnapshot snapshot = reader_.capture();
            capture_gate_.sampled(now, hz);
            const double data_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - data_start).count();
            {
                std::lock_guard<std::mutex> lock(capture_mutex_);
                published_frame_ = std::move(snapshot);
                published_data_ms_ = data_ms;
                published_is_capture_ = true;
                ++published_generation_;
            }
        }
        const double period = 1.0 / std::clamp(hz, 20, 120);
        const auto wait_until = data_start + std::chrono::duration<double>(period);
        std::unique_lock<std::mutex> wait(capture_mutex_);
        capture_cv_.wait_until(wait, wait_until, [this] {
            return capture_stop_.load(std::memory_order_acquire) ||
                   urgent_capture_.load(std::memory_order_acquire);
        });
    }
}


void FeatureEngine::tick(float screen_width, float screen_height) {
    const auto tick_start=std::chrono::steady_clock::now();
    projection_width_ = screen_width;
    projection_height_ = screen_height;
    projection_focal_basis_ = std::min(screen_width, screen_height * kReferenceAspect);
    // The decoded game-camera FOV is horizontal. Ultrawide phone viewports use
    // the engine's 16:9 reference width; narrower tablet viewports keep width.
    projection_vertical_fov_ = false;
    reader_.set_world_actors_enabled(false);
    reader_.set_scan_budget_kb(settings_.recovery_scan_kb);
    last_captured_=false;
    {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        if (seen_generation_ != published_generation_) {
            const auto old_world=frame_.local.world,old_pawn=frame_.local.pawn;
            const auto old_pid=frame_.pid;
            frame_ = published_frame_;
            last_captured_ = published_is_capture_;
            last_data_ms_ = published_data_ms_;
            seen_generation_ = published_generation_;
            if(old_pid!=frame_.pid||old_world!=frame_.local.world||old_pawn!=frame_.local.pawn)render_boxes_.clear();
        }
    }
    if (frame_.local.valid && !reader_.refresh_local(frame_)) {
        urgent_capture_.store(true, std::memory_order_release);
        capture_cv_.notify_one();
    }
    last_feature_ms_=std::max(0.0,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tick_start).count()-last_data_ms_);
}

void FeatureEngine::draw(ImDrawList* list, float width, float height) const {
    offscreen_indicator_count_ = 0;
    pose_box_count_ = 0;
    fixed_box_count_ = 0;
    draw_entity_count_ = 0;
    draw_projection_count_ = 0;
    if (!list || !settings_.boxes || !frame_.local.camera.valid) return;
    const Projection projection(frame_.local.camera, width, height, projection_vertical_fov_);
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(
        ImVec4(settings_.accent[0], settings_.accent[1], settings_.accent[2], settings_.accent[3]));
    const auto now = std::chrono::steady_clock::now();
    for (auto it = render_boxes_.begin(); it != render_boxes_.end();) {
        if (it->second.last_seen.time_since_epoch().count() != 0 &&
            now - it->second.last_seen > kRenderStateExpiry) {
            it = render_boxes_.erase(it);
        } else {
            ++it;
        }
    }
    const auto draw_box = [&](const RenderBoxState& state) {
        const float left = state.head.x - state.width * 0.5F;
        const float right = state.head.x + state.width * 0.5F;
        const float top = std::min(state.head.y, state.bottom.y);
        const float lower = std::max(state.head.y, state.bottom.y);
        if (right < 0.0F || left > width || lower < 0.0F || top > height) return false;
        list->AddRect({left, top}, {right, lower}, color, 2.0F, 0, 2.0F);
        return true;
    };
    std::unordered_set<std::uintptr_t> handled;
    handled.reserve(frame_.entities.size());
    for (const auto& entity : frame_.entities) {
        const std::uintptr_t identity = entity.actor ? entity.actor : entity.player_state;
        if (!identity) continue;
        if (entity.world_actor ||
            (entity.player_state && frame_.local.team >= 0 && entity.team == frame_.local.team) ||
            entity.distance_m > settings_.max_distance_m) {
            handled.insert(identity);
            continue;
        }
        ++draw_entity_count_;
        Vec2 bottom{}, head{};
        const Vec3 bottom_world = entity.location + Vec3{0.0F, 0.0F, -kRootToFeet};
        const Vec3 head_world = entity.location + Vec3{0.0F, 0.0F, kRootToHead};
        if (!projection.project(bottom_world, bottom) || !projection.project(head_world, head)) continue;
        ++draw_projection_count_;
        const float raw_height = std::abs(bottom.y - head.y);
        if (!std::isfinite(raw_height) || raw_height < 0.25F) continue;
        const float box_height = std::max(kMinimumBoxHeight, raw_height);
        const float center_y = (bottom.y + head.y) * 0.5F;
        head.y = center_y - box_height * 0.5F;
        bottom.y = center_y + box_height * 0.5F;
        float box_width = std::max(4.0F, box_height * 0.38F);
        auto& state = render_boxes_[identity];
        const float jump = state.initialized
            ? std::hypot(head.x - state.head.x, head.y - state.head.y) : 0.0F;
        const bool snap = !state.initialized || !std::isfinite(jump) ||
            jump > std::max(100.0F, box_height * 2.5F);
        if (!snap) {
            const float dt = std::clamp(
                std::chrono::duration<float>(now - state.last_seen).count(), 0.001F, 0.10F);
            const float distance_factor = std::clamp((entity.distance_m - 60.0F) / 300.0F, 0.0F, 1.0F);
            const float smoothing_seconds = 0.045F + distance_factor * 0.085F;
            const float alpha = 1.0F - std::exp(-dt / smoothing_seconds);
            head.x = state.head.x + (head.x - state.head.x) * alpha;
            head.y = state.head.y + (head.y - state.head.y) * alpha;
            bottom.x = state.bottom.x + (bottom.x - state.bottom.x) * alpha;
            bottom.y = state.bottom.y + (bottom.y - state.bottom.y) * alpha;
            box_width = state.width + (box_width - state.width) * alpha;
        }
        state.head = head;
        state.bottom = bottom;
        state.width = box_width;
        state.last_seen = now;
        state.initialized = true;
        handled.insert(identity);
        if (draw_box(state)) ++fixed_box_count_;
    }
    // Keep the last stable box briefly when one capture misses an otherwise live actor.
    for (const auto& [identity, state] : render_boxes_) {
        if (handled.contains(identity) || !state.initialized || now - state.last_seen > kMissingEntityHold)
            continue;
        if (draw_box(state)) ++fixed_box_count_;
    }
}

}  // namespace rebootnova
