#include "rebootnova/config_store.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace rebootnova {
namespace {
bool as_bool(const std::unordered_map<std::string, std::string>& values, const char* key, bool fallback) {
    const auto it = values.find(key); if (it == values.end()) return fallback;
    return it->second == "1" || it->second == "true" || it->second == "on";
}
int as_int(const std::unordered_map<std::string, std::string>& values, const char* key, int fallback) {
    const auto it = values.find(key); if (it == values.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}
float as_float(const std::unordered_map<std::string, std::string>& values, const char* key, float fallback) {
    const auto it = values.find(key); if (it == values.end()) return fallback;
    try { return std::stof(it->second); } catch (...) { return fallback; }
}
}

bool ConfigStore::save(const Settings& s, std::string* error) const {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) { if (error) *error = "cannot write " + path_; return false; }
    out << "version=32\nlanguage=" << static_cast<int>(s.language) << "\n";
    out << "boxes=" << (s.boxes ? "true" : "false") << "\n";
    out << "record_visible=" << (s.record_visible ? "true" : "false") << "\n";
    out << "overlay_refresh_vote=" << (s.overlay_refresh_vote ? "true" : "false") << "\n";
    out << "overlay_scale_percent=" << s.overlay_scale_percent << "\n";
    out << "performance_preset=" << s.performance_preset
        << "\nentity_refresh_hz=" << s.entity_refresh_hz
        << "\nrecovery_scan_kb=" << s.recovery_scan_kb << "\n";
    out << "fps_cap=" << s.fps_cap << "\ntheme=" << s.theme
        << "\nmax_distance_m=" << s.max_distance_m << "\n";
    for (std::size_t i = 0; i < s.accent.size(); ++i) out << "accent" << i << "=" << s.accent[i] << "\n";
    return static_cast<bool>(out);
}

bool ConfigStore::load(Settings& s, std::string* error) const {
    std::ifstream input(path_);
    if (!input) { if (error) *error = "cannot open " + path_; return false; }
    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals != std::string::npos) values[line.substr(0, equals)] = line.substr(equals + 1);
    }
    s.language = static_cast<Language>(as_int(values, "language", static_cast<int>(s.language)));
    s.boxes = as_bool(values, "boxes", s.boxes);
    s.record_visible = as_bool(values, "record_visible", s.record_visible);
    s.overlay_refresh_vote = as_bool(values, "overlay_refresh_vote", true);
    s.overlay_scale_percent = std::clamp(as_int(values, "overlay_scale_percent", 100), 50, 100);
    s.performance_preset = std::clamp(as_int(values, "performance_preset", s.performance_preset), 0, 3);
    s.entity_refresh_hz = std::clamp(as_int(values, "entity_refresh_hz", s.entity_refresh_hz), 20, 120);
    s.recovery_scan_kb = std::clamp(as_int(values, "recovery_scan_kb", s.recovery_scan_kb), 64, 2048);
    s.fps_cap = std::clamp(as_int(values, "fps_cap", s.fps_cap), 20, 144);
    s.theme = std::clamp(as_int(values, "theme", s.theme), 0, 2);
    s.max_distance_m = std::clamp(as_float(values, "max_distance_m", s.max_distance_m), 25.0F, 1000.0F);
    for (std::size_t i = 0; i < s.accent.size(); ++i)
        s.accent[i] = as_float(values, ("accent" + std::to_string(i)).c_str(), s.accent[i]);
    return true;
}

bool ConfigStore::reset(Settings& settings, std::string* error) const {
    settings = Settings{};
    return save(settings, error);
}

}  // namespace rebootnova
