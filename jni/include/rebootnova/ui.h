#pragma once

#include "rebootnova/config_store.h"
#include "rebootnova/feature_engine.h"
#include "rebootnova/offset_profile.h"
#include "rebootnova/settings.h"

#include <array>
#include <string>
#include <utility>

namespace rebootnova {

class Ui {
public:
    Ui(Settings& settings, FeatureEngine& features, ConfigStore& config, OffsetProfile& profile, std::string profile_path)
        : settings_(settings), features_(features), config_(config), profile_(profile), profile_path_(std::move(profile_path)) {}
    ~Ui();

    bool render(bool* running, bool touch_ready, const char* renderer_name);
    bool wants_surface_recreate() const { return surface_recreate_; }
    void clear_surface_recreate() { surface_recreate_ = false; }

private:
    Settings& settings_;
    FeatureEngine& features_;
    ConfigStore& config_;
    OffsetProfile& profile_;
    bool surface_recreate_{};
    bool collapsed_{};
    bool restore_expanded_position_{};
    bool has_expanded_position_{};
    bool has_expanded_size_{};
    bool resizing_{};
    float expanded_x_{};
    float expanded_y_{};
    float expanded_width_{};
    float expanded_height_{};
    float resize_start_mouse_x_{};
    float resize_start_mouse_y_{};
    float resize_start_width_{};
    float resize_start_height_{};
    float ui_scale_{1.0F};
    std::string status_;
    std::string profile_path_;

    void status_tab(bool touch_ready, const char* renderer_name);
    void config_tab();
    void esp_detail();
    void render_header_bar();
    void render_single_page(bool* running, bool touch_ready, const char* renderer_name);
    void render_collapsed_button();
    void apply_theme();

};

}  // namespace rebootnova
