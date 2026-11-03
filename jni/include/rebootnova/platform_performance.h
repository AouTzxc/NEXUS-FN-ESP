#pragma once
namespace rebootnova::performance {
struct PlatformStats {
    double prepare_ms{}, submit_ms{}, swap_ms{}, display_query_ms{}, mirror_ms{};
    int logical_width{}, logical_height{}, buffer_width{}, buffer_height{};
    int geometry_status{}, refresh_status{};
};
}
