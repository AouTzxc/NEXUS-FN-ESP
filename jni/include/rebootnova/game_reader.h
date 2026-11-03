#pragma once

#include "rebootnova/offset_profile.h"
#include "rebootnova/process_memory.h"
#include "rebootnova/types.h"
#include "rebootnova/performance.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rebootnova {

enum class BoneSlot : std::size_t { Head, Neck, LeftShoulder, RightShoulder, Spine, Pelvis, LeftFoot, RightFoot, Count };

struct LocalSnapshot {
    std::uintptr_t world{};
    std::uintptr_t game_instance{};
    std::uintptr_t local_player{};
    std::uintptr_t controller{};
    std::uintptr_t pawn{};
    std::uintptr_t player_state{};
    std::uintptr_t camera_manager{};
    int team{-1};
    CameraView camera{};
    Vec3 location{};
    Vec3 velocity{};
    bool firing{};
    bool aiming{};
    bool valid{};
    // Local pawn LastRenderTime is the on-screen clock. Occluded meshes freeze below it.
    float mesh_visibility_time{};
    bool mesh_visibility_known{};
};

struct EntitySnapshot {
    std::uintptr_t actor{};
    std::uintptr_t root_component{};
    std::uintptr_t mesh{};
    std::uintptr_t player_state{};
    int team{-1};
    Vec3 location{};
    Vec3 velocity{};
    bool visible{};
    bool visibility_known{};
    bool world_actor{};
    bool mesh_signature_fallback{};
    bool visual_mesh_aligned{};
    bool mesh_motion_fresh{};
    float distance_m{};
    std::string player_name;
    std::string player_id;
    std::string platform{"Unknown"};
    std::array<Vec3, static_cast<std::size_t>(BoneSlot::Count)> bones{};
};

struct EntityDiagnostics {
    std::int32_t actor_count{};
    std::int32_t actors_scanned{};
    std::int32_t pointer_ok{};
    std::int32_t fingerprint_read{};
    std::int32_t fingerprint_unknown{};
    std::int32_t player_fingerprint{};
    std::int32_t world_fingerprint{};
    std::int32_t player_state_ok{};
    std::int32_t player_state_candidates{};
    std::int32_t pawn_private_read{};
    std::int32_t pawn_private_matches{};
    std::int32_t structure_candidates{};
    std::int32_t structure_rejected{};
    std::int32_t actor_mesh_scans{};
    std::int32_t actor_mesh_hits{};
    std::int32_t actor_mesh_cached{};
    std::int32_t actor_component_arrays{};
    std::int32_t actor_component_hits{};
    std::int32_t team_filtered{};
    std::int32_t root_ok{};
    std::int32_t mesh_ok{};
    std::int32_t skeletal_mesh_ok{};
    std::int32_t geometry_ok{};
    std::int32_t bones_ok{};
    std::int32_t accepted_players{};
    std::int32_t accepted_fallback{};
    std::int32_t accepted_world{};
    std::int32_t mesh_signature_tests{};
    std::int32_t mesh_signature_rejected{};
    std::int32_t mesh_signature_cached{};
    std::int32_t mesh_scan_blocks_ok{};
    std::int32_t mesh_scan_blocks_failed{};
    std::int32_t mesh_scan_ranges{};
    std::int32_t mesh_scan_range_index{};
    std::uint64_t mesh_owner_offset{};
    std::int32_t mesh_owner_matches{};
    std::int32_t mesh_owner_rejected{};
    std::int32_t current_pawn_ok{};
    std::int32_t stale_pawn_suppressed{};
    std::int32_t render_fresh{};
    std::int32_t root_position_only{};
    std::int32_t mesh_aligned_boxes{};
    std::int32_t mesh_motion_fresh{};
    std::int32_t actor_hot_ranges{};
    std::string stage{"not_started"};
};

struct ActorTemporalState {
    std::uintptr_t mesh{};
    float last_render_time{};
    bool render_initialized{};
    bool observed_render_advance{};
    std::chrono::steady_clock::time_point last_render_advance{};
    Vec3 last_mesh_center{};
    bool mesh_location_initialized{};
    bool observed_mesh_motion{};
    std::chrono::steady_clock::time_point last_mesh_motion{};
};

struct PlayerIdentityCacheEntry {
    std::string player_name;
    std::string player_id;
    std::string platform{"Unknown"};
    std::chrono::steady_clock::time_point basic_refreshed{};
    std::chrono::steady_clock::time_point platform_refreshed{};
};

struct MeshProbeDiagnostics {
    std::uintptr_t address{};
    bool bone_array{};
    bool component_transform{};
    bool render_time{};
    bool bones{};
    bool accepted{};
    bool projected{};
    float render_time_value{};
    float distance_m{};
    Vec3 location{};
    Vec2 screen{};
};

struct FrameSnapshot {
    LocalSnapshot local{};
    std::vector<EntitySnapshot> entities;
    EntityDiagnostics entity_diagnostics{};
    MeshProbeDiagnostics local_mesh_probe{};
    MeshProbeDiagnostics verified_mesh_probe{};
    std::chrono::steady_clock::time_point timestamp{};
    std::uint64_t sequence{};
    pid_t pid{-1};
    std::uintptr_t module_base{};
    std::string module_name;
    std::string error;
};

class GameReader {
public:
    GameReader(ProcessMemory& memory, OffsetProfile& profile);
    void set_profile(const OffsetProfile& profile);
    void set_world_actors_enabled(bool enabled) { include_world_actors_.store(enabled, std::memory_order_relaxed); }
    const OffsetProfile& profile() const { return profile_; }
    FrameSnapshot capture();
    bool refresh_local(FrameSnapshot& frame);
    void set_scan_budget_kb(int kb){scan_budget_bytes_.store(std::size_t(std::clamp(kb,64,2048))*1024, std::memory_order_relaxed);}

    void remember_external_names(
        const std::vector<std::pair<std::string, std::string>>& names);

private:
    ProcessMemory& memory_;
    OffsetProfile& profile_;
    mutable std::mutex mutex_;
    std::uint64_t sequence_{};
    std::atomic<std::size_t> scan_budget_bytes_{512*1024};
    std::size_t scan_remaining_{};
    std::chrono::steady_clock::time_point next_diagnostic_probe_{};
    MeshProbeDiagnostics cached_local_probe_{},cached_verified_probe_{};
    std::size_t take_scan_budget(std::size_t request){return performance::take_budget(scan_remaining_,request);}

    std::uintptr_t mesh_scan_owner_{};
    std::uintptr_t mesh_scan_cursor_{};
    std::uintptr_t mesh_scan_end_{};
    std::vector<MemoryRange> mesh_scan_ranges_;
    std::size_t mesh_scan_range_index_{};
    std::vector<std::uintptr_t> discovered_meshes_;
    std::unordered_map<std::uintptr_t, std::uint16_t> mesh_cache_misses_;
    std::uint64_t mesh_signature_tests_{};
    std::uint64_t mesh_signature_rejected_{};
    std::uint64_t mesh_scan_blocks_ok_{};
    std::uint64_t mesh_scan_blocks_failed_{};
    std::uintptr_t mesh_owner_offset_{};
    pid_t actor_scan_pid_{-1};
    std::uintptr_t actor_scan_owner_{};
    std::vector<MemoryRange> actor_scan_ranges_;
    std::vector<MemoryRange> actor_hot_ranges_;
    std::size_t actor_scan_range_index_{};
    std::uintptr_t actor_scan_cursor_{};
    std::size_t actor_cold_range_index_{};
    std::uintptr_t actor_cold_cursor_{};
    bool actor_scan_complete_{};
    std::vector<std::uintptr_t> discovered_player_actors_;
    std::unordered_map<std::uintptr_t, std::uint16_t> actor_cache_misses_;
    std::unordered_map<std::uintptr_t, std::uintptr_t> actor_mesh_cache_;
    std::unordered_map<std::uintptr_t, std::uint16_t> actor_mesh_probe_misses_;
    std::unordered_map<std::uintptr_t, ActorTemporalState> actor_temporal_;
    std::unordered_map<std::uintptr_t, PlayerIdentityCacheEntry> player_identity_cache_;
    std::unordered_map<std::uintptr_t, std::string> scoreboard_display_names_;
    std::unordered_map<std::string, std::string> persistent_account_display_names_;
    std::vector<MemoryRange> scoreboard_scan_ranges_;
    std::size_t scoreboard_scan_range_index_{};
    std::uintptr_t scoreboard_scan_cursor_{};
    std::uintptr_t scoreboard_scan_hint_{};
    std::chrono::steady_clock::time_point next_scoreboard_scan_{};
    std::unordered_map<std::string, std::string> account_display_names_;
    std::vector<MemoryRange> party_identity_scan_ranges_;
    std::size_t party_identity_scan_range_index_{};
    std::uintptr_t party_identity_scan_cursor_{};
    std::size_t party_identity_scan_bytes_{};
    std::chrono::steady_clock::time_point next_party_identity_scan_{};
    std::chrono::steady_clock::time_point next_platform_probe_{};
    std::chrono::steady_clock::time_point next_process_probe_{};
    pid_t observed_pid_{-1};
    std::uintptr_t observed_module_base_{};
    std::uintptr_t observed_world_{};
    int local_read_failures_{};
    std::atomic<bool> include_world_actors_{false};

    bool ensure_attached(std::string& error);
    void reset_process_state(pid_t pid, std::uintptr_t module_base = 0);
    void reset_world_state(std::uintptr_t world);
    bool read_local(LocalSnapshot& out, std::string& error) const;
    void read_entities(const LocalSnapshot& local, std::vector<EntitySnapshot>& out, EntityDiagnostics& diagnostics);
    CameraView decode_camera(const CameraCacheRaw& raw) const;
    bool read_bones(EntitySnapshot& entity) const;
    std::uintptr_t resolve_mesh_owner_offset(std::uintptr_t local_mesh, std::uintptr_t local_pawn);
    bool update_render_freshness(std::uintptr_t actor, std::uintptr_t mesh,
                                 const std::optional<float>& render_time,
                                 std::chrono::steady_clock::time_point now);
    void apply_render_visibility(EntitySnapshot& entity, const LocalSnapshot& local,
                                 const std::optional<float>& render_time,
                                 std::chrono::steady_clock::time_point now);
    bool update_mesh_motion_freshness(std::uintptr_t actor, std::uintptr_t mesh,
                                      const Vec3& mesh_center,
                                      std::chrono::steady_clock::time_point now);
    std::optional<EntitySnapshot> probe_mesh(std::uintptr_t mesh, const LocalSnapshot& local,
                                             MeshProbeDiagnostics& diagnostics) const;
    void discover_mesh_entities(const LocalSnapshot& local, std::vector<EntitySnapshot>& out,
                                EntityDiagnostics& diagnostics);
    void scan_player_actor_ranges(const LocalSnapshot& local, const std::vector<MemoryRange>& ranges,
                                  std::size_t& range_index, std::uintptr_t& cursor,
                                  std::size_t budget);
    void discover_player_actors(const LocalSnapshot& local, std::vector<EntitySnapshot>& out,
                                EntityDiagnostics& diagnostics);
    bool validate_skeletal_mesh(std::uintptr_t mesh) const;
    void scan_party_identity_cache();
    void scan_scoreboard_identity_cache(const LocalSnapshot& local,
                                        std::vector<EntitySnapshot>& entities);
    void load_persistent_name_cache();
    bool remember_persistent_name(std::string_view account_id, std::string_view display_name);
    void persist_name_cache() const;
    void populate_player_identity(EntitySnapshot& entity);
    std::uintptr_t resolve_actor_mesh(std::uintptr_t actor, std::uintptr_t local_mesh,
                                      std::size_t& probe_budget, EntityDiagnostics& diagnostics);
};

}  // namespace rebootnova
