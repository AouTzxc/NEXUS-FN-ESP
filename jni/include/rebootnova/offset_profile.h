#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace rebootnova {

enum class Evidence { Verified, Hypothesis, DevicePending };

struct OffsetField {
    std::uint64_t value{};
    Evidence evidence{Evidence::DevicePending};
    std::string source;
};

struct OffsetProfile {
    std::string build{"57566230"};
    std::string package_name{"com.epicgames.fortnite"};
    std::string module_name{"libUnreal.so"};

    std::uint64_t gworld{0x1D590D20};
    std::uint64_t world_owning_game_instance{0x270};
    std::uint64_t world_actor_array{0x210};
    std::uint64_t level_actor_array{0x40};
    std::uint64_t game_instance_local_players{0x38};
    std::uint64_t local_player_controller{0x30};
    std::uint64_t controller_acknowledged_pawn{0x308};
    std::uint64_t controller_camera_manager{0x318};
    // Input-state offsets are build-specific and remain device-pending.
    std::uint64_t controller_firing_state{};
    std::uint64_t controller_aiming_state{};
    // Historical V3 aim write fields. Zero until validated for this CL.
    std::uint64_t controller_aim_pitch_delta{};
    std::uint64_t controller_aim_yaw_delta{};
    std::uint64_t pawn_player_state{0x280};
    std::uint64_t player_state_team{0xF40};
    std::uint64_t player_state_pawn_private{};
    std::uint64_t actor_class_fingerprint{0x58};
    std::uint64_t actor_player_state{0x280};
    std::uint64_t actor_root_component{0x1B0};
    std::uint64_t character_mesh{0x2E0};
    std::uint64_t scene_component_location{0x200};
    std::uint64_t scene_component_velocity{};
    std::uint64_t camera_cache{0x29D0};
    std::uint64_t camera_cache_size{0x34};
    std::uint64_t mesh_bone_array{0x660};
    std::uint64_t mesh_bone_array_fallback{};
    std::uint64_t mesh_component_to_world{0x1E0};
    // CL-56878558 LastRenderTime. CL-57566230 uses +0x294 (builtin_57566230).
    std::uint64_t mesh_visibility_time{0x2E4};
    std::uint64_t bone_stride{0x60};
    std::uint32_t max_entities{512};
    bool allow_unverified_fields{false};

    static OffsetProfile builtin_56878558();
    static OffsetProfile builtin_57316517();
    static OffsetProfile builtin_57566230();
    static OffsetProfile builtin_57762904();
    static OffsetProfile builtin_59343437();
    bool load_ini(const std::string& path, std::string* error = nullptr);
    bool save_ini(const std::string& path, std::string* error = nullptr) const;
    std::vector<std::string> validate() const;
    const std::unordered_map<std::string, OffsetField>& provenance() const { return provenance_; }

private:
    std::unordered_map<std::string, OffsetField> provenance_;
    void seed_provenance();
};

std::uint64_t decrypt_world(std::uint64_t encoded);
std::uint64_t decrypt_world(std::uint64_t encoded, const OffsetProfile& profile);
const char* evidence_name(Evidence evidence);

}  // namespace rebootnova
