#pragma once

#include <array>
#include <cstring>
#include <string>

namespace rebootnova {

enum class Language { Chinese, English };

struct Settings {
    Language language{Language::Chinese};
    bool boxes{true};
    bool record_visible{true};
    int fps_cap{120};
    int theme{0};
    float max_distance_m{1000.0F};
    std::array<float, 4> accent{0.20F, 0.62F, 1.0F, 1.0F};
    int performance_preset{3};
    int entity_refresh_hz{60};
    int recovery_scan_kb{512};
    int overlay_scale_percent{100};
    bool overlay_refresh_vote{true};
};

inline const char* localized_zh(const char* en) {
    if (!en) return "";
    if (std::strcmp(en, "SETTINGS") == 0) return "\xE8\xAE\xBE\xE7\xBD\xAE";
    if (std::strcmp(en, "Player Boxes") == 0) return "\xE7\x8E\xA9\xE5\xAE\xB6\xE6\x96\xB9\xE6\xA1\x86";
    if (std::strcmp(en, "Runtime Status") == 0) return "\xE8\xBF\x90\xE8\xA1\x8C\xE7\x8A\xB6\xE6\x80\x81";
    if (std::strcmp(en, "Configuration") == 0) return "\xE9\x85\x8D\xE7\xBD\xAE";
    if (std::strcmp(en, "Exit") == 0) return "\xE9\x80\x80\xE5\x87\xBA";
    if (std::strcmp(en, "Line thickness") == 0) return "\xE7\xBA\xBF\xE6\x9D\xA1\xE7\xB2\x97\xE7\xBB\x86";
    if (std::strcmp(en, "Opacity") == 0) return "\xE4\xB8\x8D\xE9\x80\x8F\xE6\x98\x8E\xE5\xBA\xA6";
    if (std::strcmp(en, "Renderer") == 0) return "\xE6\xB8\xB2\xE6\x9F\x93\xE5\x99\xA8";
    if (std::strcmp(en, "Touch") == 0) return "\xE8\xA7\xA6\xE6\x8E\xA7";
    if (std::strcmp(en, "Ready") == 0) return "\xE5\xB0\xB1\xE7\xBB\xAA";
    if (std::strcmp(en, "Unavailable") == 0) return "\xE4\xB8\x8D\xE5\x8F\xAF\xE7\x94\xA8";
    if (std::strcmp(en, "Game status") == 0) return "\xE6\xB8\xB8\xE6\x88\x8F\xE7\x8A\xB6\xE6\x80\x81";
    if (std::strcmp(en, "Attached") == 0) return "\xE5\xB7\xB2\xE8\xBF\x9E\xE6\x8E\xA5";
    if (std::strcmp(en, "Waiting") == 0) return "\xE7\xAD\x89\xE5\xBE\x85\xE8\xBF\x9B\xE7\xA8\x8B";
    if (std::strcmp(en, "Entities") == 0) return "\xE5\xAE\x9E\xE4\xBD\x93\xE6\x95\xB0";
    if (std::strcmp(en, "Save Config") == 0) return "\xE4\xBF\x9D\xE5\xAD\x98\xE9\x85\x8D\xE7\xBD\xAE";
    if (std::strcmp(en, "Config saved") == 0) return "\xE9\x85\x8D\xE7\xBD\xAE\xE5\xB7\xB2\xE4\xBF\x9D\xE5\xAD\x98";
    if (std::strcmp(en, "Load Config") == 0) return "\xE5\x8A\xA0\xE8\xBD\xBD\xE9\x85\x8D\xE7\xBD\xAE";
    if (std::strcmp(en, "Config loaded") == 0) return "\xE9\x85\x8D\xE7\xBD\xAE\xE5\xB7\xB2\xE5\x8A\xA0\xE8\xBD\xBD";
    if (std::strcmp(en, "Reset Config") == 0) return "\xE6\x81\xA2\xE5\xA4\x8D\xE9\xBB\x98\xE8\xAE\xA4";
    if (std::strcmp(en, "Defaults restored") == 0) return "\xE5\xB7\xB2\xE6\x81\xA2\xE5\xA4\x8D\xE9\xBB\x98\xE8\xAE\xA4";
    if (std::strcmp(en, "Path") == 0) return "\xE8\xB7\xAF\xE5\xBE\x84";
    if (std::strcmp(en, "Interface language") == 0) return "\xE7\x95\x8C\xE9\x9D\xA2\xE8\xAF\xAD\xE8\xA8\x80";
    if (std::strcmp(en, "This build draws player boxes only.") == 0) return "\xE8\xBF\x99\xE4\xB8\xAA\xE7\x89\x88\xE6\x9C\xAC\xE5\x8F\xAA\xE7\x94\xBB\xE7\x8E\xA9\xE5\xAE\xB6\xE6\x96\xB9\xE6\xA1\x86\xE3\x80\x82";
    if (std::strcmp(en, "Box Color") == 0) return "\xE6\x96\xB9\xE6\xA1\x86\xE9\xA2\x9C\xE8\x89\xB2";
    if (std::strcmp(en, "Save, load or restore ESP settings.") == 0) return "\xE4\xBF\x9D\xE5\xAD\x98\xE3\x80\x81\xE5\x8A\xA0\xE8\xBD\xBD\xE6\x88\x96\xE6\x81\xA2\xE5\xA4\x8D\xE9\x80\x8F\xE8\xA7\x86\xE8\xAE\xBE\xE7\xBD\xAE\xE3\x80\x82";
    if (std::strcmp(en, "Exit closes the overlay.") == 0) return "\xE9\x80\x80\xE5\x87\xBA\xE5\xB0\x86\xE5\x85\xB3\xE9\x97\xAD\xE6\x82\xAC\xE6\xB5\xAE\xE5\xB1\x82\xE3\x80\x82";
    if (std::strcmp(en, "Module enabled") == 0) return "\xE5\x8A\x9F\xE8\x83\xBD\xE5\xB7\xB2\xE5\x90\xAF\xE7\x94\xA8";
    if (std::strcmp(en, "Module disabled") == 0) return "\xE5\x8A\x9F\xE8\x83\xBD\xE5\xB7\xB2\xE5\x85\xB3\xE9\x97\xAD";
    if (std::strcmp(en, "Boxes only") == 0) return "\xE5\x8F\xAA\xE6\x98\xBE\xE7\xA4\xBA\xE6\x96\xB9\xE6\xA1\x86";
    if (std::strcmp(en, "Process and rendering status") == 0) return "\xE8\xBF\x9B\xE7\xA8\x8B\xE4\xB8\x8E\xE6\xB8\xB2\xE6\x9F\x93\xE7\x8A\xB6\xE6\x80\x81";
    if (std::strcmp(en, "Save, load, reset") == 0) return "\xE4\xBF\x9D\xE5\xAD\x98\xE3\x80\x81\xE5\x8A\xA0\xE8\xBD\xBD\xE3\x80\x81\xE6\x81\xA2\xE5\xA4\x8D";
    if (std::strcmp(en, "Close overlay") == 0) return "\xE5\x85\xB3\xE9\x97\xAD\xE6\x82\xAC\xE6\xB5\xAE\xE5\xB1\x82";
    if (std::strcmp(en, "Features") == 0) return "\xE5\x8A\x9F\xE8\x83\xBD";
    if (std::strcmp(en, "Display Info") == 0) return "\xE6\x98\xBE\xE7\xA4\xBA\xE4\xBF\xA1\xE6\x81\xAF";
    if (std::strcmp(en, "Minimize") == 0) return "\xE6\x9C\x80\xE5\xB0\x8F\xE5\x8C\x96";
    if (std::strcmp(en, "Single ESP") == 0) return "\xE5\x8D\x95\xE9\x80\x8F\xE8\xA7\x86";
    return en;
}

inline const char* tr(Language language, const char*, const char* en) {
    return language == Language::Chinese ? localized_zh(en) : en;
}

}  // namespace rebootnova
