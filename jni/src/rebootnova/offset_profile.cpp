#include "rebootnova/offset_profile.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <sstream>

namespace rebootnova {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parse_u64(const std::string& text, std::uint64_t& out) {
    const std::string value = trim(text);
    if (value.empty()) return false;
    int base = 10;
    std::size_t start = 0;
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        base = 16;
        start = 2;
    }
    const auto result = std::from_chars(value.data() + start, value.data() + value.size(), out, base);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool parse_bool(const std::string& value, bool& out) {
    const std::string normalized = trim(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") { out = true; return true; }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") { out = false; return true; }
    return false;
}

}  // namespace

std::uint64_t decrypt_world(std::uint64_t encoded) {
    constexpr auto ror64 = [](std::uint64_t value, unsigned int count) {
        return (value >> count) | (value << (64U - count));
    };
    // Fortnite 42.10.0 / CL-57566230, recovered from the GetWorld fallback
    // at RVA 0x0933FDE4:
    //   adrp x8, #0x1D590000; ldr x8, [x8,#0xD20]
    //   add  x8, x8, #-0x1847AFF
    //   eor  x0, x9, x8, ror #61       ; x9 = 0x86213C84
    if (encoded & 0x8000000000000000ULL) {
        return 0x0000000086213C84ULL ^
               ror64(encoded + 0xFFFFFFFFFE7B8501ULL, 61U);
    }
    // Fortnite 42.00.0 / CL-57316517 fallback, kept for older profiles.
    return (0x00130AEE8D6352FBULL ^ ror64(encoded, 35U)) *
           0x19C887B736261A09ULL;
}

std::uint64_t decrypt_world(std::uint64_t encoded, const OffsetProfile& profile) {
    if (profile.build == "59343437") {
        // V: CL-59343437 (Fortnite 42.40.0) world getter @ RVA 0x0C8A97DC, IDA MCP
        // session cl593 plus the AGG live pointer chain. Codec mode 3, no rotation:
        //   bswap64(encoded ^ 0x01296A4C) - 0xF7FED09E   (uint64 wrap)
        // Live vector: 0x9EA204CB6F296A4C -> 0x6DD305D200.
        std::uint64_t value = encoded ^ 0x01296A4CULL;
        value = ((value & 0x00FF00FF00FF00FFULL) << 8) |
                ((value >> 8) & 0x00FF00FF00FF00FFULL);
        value = ((value & 0x0000FFFF0000FFFFULL) << 16) |
                ((value >> 16) & 0x0000FFFF0000FFFFULL);
        value = (value << 32) | (value >> 32);
        return value - 0xF7FED09EULL;
    }
    if (profile.build != "57762904") return decrypt_world(encoded);
    // V: E-RESTART-23245 and agg-device-test-layout.json. No rotation in this CL.
    std::uint64_t value = encoded ^ 0x0102609BULL;
    value = ((value & 0x00FF00FF00FF00FFULL) << 8) |
            ((value >> 8) & 0x00FF00FF00FF00FFULL);
    value = ((value & 0x0000FFFF0000FFFFULL) << 16) |
            ((value >> 16) & 0x0000FFFF0000FFFFULL);
    value = (value << 32) | (value >> 32);
    return value - 0xB7F74389ULL;
}

const char* evidence_name(Evidence evidence) {
    switch (evidence) {
        case Evidence::Verified: return "V";
        case Evidence::Hypothesis: return "H";
        case Evidence::DevicePending: return "T";
    }
    return "T";
}

OffsetProfile OffsetProfile::builtin_56878558() {
    OffsetProfile profile;
    profile.seed_provenance();
    return profile;
}

OffsetProfile OffsetProfile::builtin_57316517() {
    OffsetProfile profile;
    // CL-57316517 retains the controller delta pair used by the normal
    // (non-silent) aim path. Firing/ADS state fields remain disabled.
    profile.controller_aim_pitch_delta = 0x4A0;
    profile.controller_aim_yaw_delta = 0x4A8;
    // Mesh+0x2E4 is zero in this build. Mesh+0x53C advances for a rendered
    // player and remains frozen for the same player behind a solid wall.
    profile.mesh_visibility_time = 0x53C;
    profile.seed_provenance();
    return profile;
}

OffsetProfile OffsetProfile::builtin_57566230() {
    OffsetProfile profile;
    profile.build = "57566230";
    profile.gworld = 0x1D590D20;
    profile.world_actor_array = 0x218;
    profile.level_actor_array = 0x138;
    profile.world_owning_game_instance = 0x278;
    profile.game_instance_local_players = 0x38;
    profile.local_player_controller = 0x30;
    profile.controller_acknowledged_pawn = 0x308;
    profile.controller_camera_manager = 0x318;
    profile.controller_aim_pitch_delta = 0x4A0;
    profile.controller_aim_yaw_delta = 0x4A8;
    profile.pawn_player_state = 0x280;
    profile.player_state_team = 0xF40;
    profile.actor_class_fingerprint = 0x58;
    profile.actor_player_state = 0x280;
    profile.actor_root_component = 0x1B0;
    profile.character_mesh = 0x2E0;
    profile.scene_component_location = 0x200;
    profile.camera_cache = 0x29D0;
    profile.camera_cache_size = 0x34;
    profile.mesh_bone_array = 0x658;
    profile.mesh_component_to_world = 0x1E0;
    profile.mesh_visibility_time = 0x294;
    profile.bone_stride = 0x60;
    profile.seed_provenance();
    return profile;
}

OffsetProfile OffsetProfile::builtin_57762904() {
    auto profile = builtin_57566230();
    profile.build = "57762904";
    profile.gworld = 0x1D5D8D20;
    profile.world_owning_game_instance = 0x268;
    profile.world_actor_array = 0x208;
    profile.level_actor_array = 0x208;
    // T: old-build semantics have not been validated. Keep the modules compiled,
    // but do not expose stale offsets as active fields in the new default profile.
    profile.controller_aim_pitch_delta = 0x4A0; // V: native input + AGG 0.6-degree response.
    profile.controller_aim_yaw_delta = 0x4A8;
    profile.player_state_team = 0;
    profile.mesh_visibility_time = 0x534; // V: render-time-roster / screen-correlation evidence.
    profile.allow_unverified_fields = false;
    profile.seed_provenance();
    return profile;
}

OffsetProfile OffsetProfile::builtin_59343437() {
    // Fortnite 42.40.0 / CL-59343437. Only the fields this box-only ESP reads
    // are carried here. The CL-59343437 server profile also reports name,
    // scoreboard, model-chams, vehicle-marker and bone-bank fields that this
    // build never touches, so they are deliberately not adopted.
    OffsetProfile profile;
    profile.build = "59343437";
    profile.gworld = 0x1D92CF70;
    profile.world_owning_game_instance = 0x268;
    profile.world_actor_array = 0x208;
    profile.level_actor_array = 0x48;
    profile.game_instance_local_players = 0x38;
    profile.local_player_controller = 0x30;
    profile.controller_acknowledged_pawn = 0x308;
    profile.controller_camera_manager = 0x318;
    profile.controller_aim_pitch_delta = 0x4A0;
    profile.controller_aim_yaw_delta = 0x4A8;
    profile.pawn_player_state = 0x280;
    profile.player_state_team = 0xF49;  // V: byte-wide TeamIndex; 0xff means no team.
    profile.actor_class_fingerprint = 0x58;
    profile.actor_player_state = 0x280;
    profile.actor_root_component = 0x1B0;
    profile.character_mesh = 0x2E0;
    profile.scene_component_location = 0x200;
    profile.camera_cache = 0x1550;
    profile.camera_cache_size = 0x34;
    profile.mesh_bone_array = 0x650;
    profile.mesh_component_to_world = 0x1E0;  // V: scale still at transform +0x40.
    profile.mesh_visibility_time = 0x338;
    profile.bone_stride = 0x60;
    profile.allow_unverified_fields = false;
    profile.seed_provenance();
    return profile;
}

void OffsetProfile::seed_provenance() {
    provenance_.clear();
    auto verified = [this](const char* name, std::uint64_t value, const char* source) {
        provenance_[name] = {value, value == 0 ? Evidence::DevicePending : Evidence::Verified, source};
    };
    auto hypothesis = [this](const char* name, std::uint64_t value, const char* source) {
        provenance_[name] = {value, Evidence::Hypothesis, source};
    };
    if (build == "57566230") {
        verified("gworld", gworld,
                 "CL-57566230 GetWorld @ RVA 0x0933FDE4; adb decoded UWorld");
    } else {
        verified("gworld", gworld,
                 "CL-57316517 GetWorld @ RVA 0x0937875C; live decoded UWorld");
    }
    verified("world_owning_game_instance", world_owning_game_instance, "live UWorld chain");
    verified("world_actor_array", world_actor_array,
             build == "57566230"
                 ? "CL-57566230 adb UWorld+0x218 Levels TArray, count=7 in frontend sample"
                 : "CL-57316517 live UWorld+0x210 Levels TArray");
    verified("level_actor_array", level_actor_array,
             build == "57566230"
                 ? "CL-57566230 adb Level+0x138 actor TArray candidate; frontend total=375"
                 : "CL-57316517 live Level+0x40 Actors TArray; 4,781 actors across 27 levels");
    verified("game_instance_local_players", game_instance_local_players, "live GameInstance chain");
    verified("local_player_controller", local_player_controller, "live LocalPlayer chain");
    verified("controller_acknowledged_pawn", controller_acknowledged_pawn,
             build == "57566230"
                 ? "CL-57566230 pending in-match pawn check; kept at historical APawn offset"
                 : "CL-57316517 live controller -> Pawn read through LAN ATG");
    verified("controller_camera_manager", controller_camera_manager,
             build == "57566230"
                 ? "CL-57566230 adb Controller+0x318 -> CameraManager, CameraCache+0x29D0"
                 : "CL-57316517 live controller chain");
    hypothesis("controller_firing_state", controller_firing_state,
               "UE input-state field; device validation pending");
    hypothesis("controller_aiming_state", controller_aiming_state,
               "UE input-state field; device validation pending");
    if ((build == "57316517" || build == "57566230") && controller_aim_pitch_delta == 0x4A0 &&
        controller_aim_yaw_delta == 0x4A8) {
        verified("controller_aim_pitch_delta", controller_aim_pitch_delta,
                 "ARM64 static match; Controller +0x4A0 normal-aim delta field");
        verified("controller_aim_yaw_delta", controller_aim_yaw_delta,
                 "ARM64 static match; Controller +0x4A8 normal-aim delta field");
    } else {
        hypothesis("controller_aim_pitch_delta", controller_aim_pitch_delta,
                   "Historical ARM64 V3AimManager +0x4A0; build-specific validation pending");
        hypothesis("controller_aim_yaw_delta", controller_aim_yaw_delta,
                   "Historical ARM64 V3AimManager +0x4A8; build-specific validation pending");
    }
    verified("pawn_player_state", pawn_player_state, "live Pawn -> PlayerState");
    verified("player_state_team", player_state_team, "live PlayerState team read");
    verified("player_state_pawn_private", player_state_pawn_private, "ACE PID 18529 current Pawn backlink");
    verified("actor_class_fingerprint", actor_class_fingerprint, "live player fingerprint @ Actor+0x58");
    verified("actor_player_state", actor_player_state, "live player actor chain @ Actor+0x280");
    verified("actor_root_component", actor_root_component,
             build == "57566230"
                 ? "CL-57566230 uses the stable character RootComponent offset"
                 : "live actor root");
    verified("character_mesh", character_mesh,
             "live Pawn -> SkeletalMesh read");
    verified("scene_component_location", scene_component_location,
             build == "57566230"
                 ? "CL-57566230 uses the stable LWC RootComponent location offset"
                 : "live RootComponent location");
    verified("camera_cache", camera_cache, "live 0x34-byte camera copy");
    verified("camera_cache_size", camera_cache_size, "donor 0x34-byte camera copy");
    verified("mesh_bone_array", mesh_bone_array,
             build == "57566230"
                 ? "CL-57566230 adb player-like actor Mesh+0x658 bone TArray"
                 : "CL-57316517 live SkeletalMesh -> BoneArray read through LAN ATG");
    verified("mesh_bone_array_fallback", mesh_bone_array_fallback, "donor batched mesh read @ 0x251B58");
    verified("mesh_component_to_world", mesh_component_to_world,
             "CL-57316517 live SkeletalMesh -> ComponentToWorld read through LAN ATG");
    if (build == "57566230" && mesh_visibility_time == 0x294) {
        verified("mesh_visibility_time", mesh_visibility_time,
                 "CL-57566230 AGG live match: Mesh+0x294 advances only for rendered players; +0x53C is zero");
    } else if (build == "57316517" && mesh_visibility_time == 0x53C) {
        verified("mesh_visibility_time", mesh_visibility_time,
                 "CL-57316517 live Mesh+0x53C: advances while rendered and freezes behind wall");
    } else {
        verified("mesh_visibility_time", mesh_visibility_time,
                 "live Mesh render-time sample through LAN ATG");
    }
    hypothesis("scene_component_velocity", scene_component_velocity, "UE layout; device validation pending");
    verified("bone_stride", bone_stride, "CL-56878558 live BoneArray transform stride through LAN ATG");
    if (build == "57762904") {
        // V means validated read layout in the captured runs, not visual accuracy,
        // all character types, team semantics, or functioning Tracking/Model.
        for (auto& [name, field] : provenance_) {
            field.evidence = Evidence::DevicePending;
            field.source = "CL-57762904 T: inactive until device validation";
        }
        for (const char* name : {"gworld", "world_owning_game_instance", "world_actor_array",
             "level_actor_array", "game_instance_local_players", "local_player_controller",
             "controller_acknowledged_pawn", "controller_camera_manager", "actor_root_component",
             "scene_component_location"}) {
            auto& field = provenance_.at(name);
            field.evidence = Evidence::Verified;
            field.source = "CL-57762904 V: E-RESTART-23245; three WorldContext equality samples";
        }
        for (const char* name : {"pawn_player_state", "actor_player_state", "actor_class_fingerprint",
             "character_mesh", "camera_cache", "camera_cache_size", "mesh_bone_array",
             "mesh_component_to_world", "bone_stride"}) {
            auto& field = provenance_.at(name);
            field.evidence = Evidence::Verified;
            field.source = "CL-57762904 V read layout: agg-device-test-layout.json; projection/semantics pending";
        }
        for(const char* name : {"controller_aim_pitch_delta","controller_aim_yaw_delta"}) {
            auto& field=provenance_.at(name);field.evidence=Evidence::Verified;
            field.source="CL-57762904 V: performance-v3/aim-native-input.json and aim-small-input-verification.json; +0.6 degree observed and consumed";
        }
        auto& visibility = provenance_.at("mesh_visibility_time");
        visibility.evidence = Evidence::Verified;
        visibility.source = "CL-57762904 V: v2 render-time-roster and screen-correlation; render freshness, not a physics raycast";
    }
    if (build == "59343437") {
        // V here means the read layout matched the CL-59343437 IDA MCP session
        // (cl593) and the AGG live pointer chain. It does not certify visual
        // accuracy, every character type, or the recent-render visibility
        // heuristic, which is a render-clock comparison and not a raycast LOS test.
        for (auto& [name, field] : provenance_) {
            field.evidence = Evidence::DevicePending;
            field.source = "CL-59343437 T: not carried as an active ESP field";
        }
        for (const char* name : {"gworld", "world_owning_game_instance", "world_actor_array",
             "level_actor_array", "game_instance_local_players", "local_player_controller",
             "controller_acknowledged_pawn", "controller_camera_manager", "pawn_player_state",
             "player_state_team", "actor_class_fingerprint", "actor_player_state",
             "actor_root_component", "character_mesh", "scene_component_location",
             "camera_cache", "camera_cache_size", "mesh_bone_array", "mesh_component_to_world",
             "bone_stride"}) {
            auto& field = provenance_.at(name);
            field.evidence = Evidence::Verified;
            field.source = "CL-59343437 V: IDA MCP session cl593 + AGG live chain (runtime_verified)";
        }
        provenance_.at("gworld").source =
            "CL-59343437 V: world getter @ RVA 0x0C8A97DC, slot 0x1D92CF70; codec mode 3 decodes 0x9EA204CB6F296A4C to 0x6DD305D200";
        provenance_.at("player_state_team").source =
            "CL-59343437 V: TeamIndex is a byte at PlayerState+0xF49 and 0xff is the no-team sentinel; read 1 byte, not int32";
        provenance_.at("mesh_component_to_world").source =
            "CL-59343437 V: ComponentToWorld at Mesh+0x1E0 with scale at +0x40, matching RemoteTransform96";
        provenance_.at("mesh_visibility_time").source =
            "CL-59343437 V: Mesh+0x338 render-time progression correlated with the world game clock; recent-render heuristic, not raycast LOS";
        for (const char* name : {"controller_aim_pitch_delta", "controller_aim_yaw_delta"}) {
            auto& field = provenance_.at(name);
            field.evidence = Evidence::Verified;
            field.source = "CL-59343437 V: double accumulation at Controller+0x4A0/+0x4A8; box-only build never writes them";
        }
    }
}

bool OffsetProfile::load_ini(const std::string& path, std::string* error) {
    std::ifstream input(path);
    if (!input) { if (error) *error = "cannot open profile: " + path; return false; }
    std::string section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') { section = trim(line.substr(1, line.size() - 2)); continue; }
        const auto equals = line.find('=');
        if (equals == std::string::npos) { if (error) *error = "invalid profile line " + std::to_string(line_number); return false; }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key == "build") build = value;
        else if (key == "package") package_name = value;
        else if (key == "module") module_name = value;
        else if (key == "allow_unverified_fields") { if (!parse_bool(value, allow_unverified_fields)) goto invalid; }
        else {
            std::uint64_t parsed{};
            if (!parse_u64(value, parsed)) goto invalid;
#define RN_ASSIGN_OFFSET(name) if (key == #name) name = parsed; else
            RN_ASSIGN_OFFSET(gworld)
            RN_ASSIGN_OFFSET(world_owning_game_instance)
            RN_ASSIGN_OFFSET(world_actor_array)
            RN_ASSIGN_OFFSET(level_actor_array)
            RN_ASSIGN_OFFSET(game_instance_local_players)
            RN_ASSIGN_OFFSET(local_player_controller)
            RN_ASSIGN_OFFSET(controller_acknowledged_pawn)
            RN_ASSIGN_OFFSET(controller_camera_manager)
            RN_ASSIGN_OFFSET(controller_firing_state)
            RN_ASSIGN_OFFSET(controller_aiming_state)
            RN_ASSIGN_OFFSET(controller_aim_pitch_delta)
            RN_ASSIGN_OFFSET(controller_aim_yaw_delta)
            RN_ASSIGN_OFFSET(pawn_player_state)
            RN_ASSIGN_OFFSET(player_state_team)
            RN_ASSIGN_OFFSET(player_state_pawn_private)
            RN_ASSIGN_OFFSET(actor_class_fingerprint)
            RN_ASSIGN_OFFSET(actor_player_state)
            RN_ASSIGN_OFFSET(actor_root_component)
            RN_ASSIGN_OFFSET(character_mesh)
            RN_ASSIGN_OFFSET(scene_component_location)
            RN_ASSIGN_OFFSET(scene_component_velocity)
            RN_ASSIGN_OFFSET(camera_cache)
            RN_ASSIGN_OFFSET(camera_cache_size)
            RN_ASSIGN_OFFSET(mesh_bone_array)
            RN_ASSIGN_OFFSET(mesh_bone_array_fallback)
            RN_ASSIGN_OFFSET(mesh_component_to_world)
            RN_ASSIGN_OFFSET(mesh_visibility_time)
            RN_ASSIGN_OFFSET(bone_stride)
            if (key == "max_entities") max_entities = static_cast<std::uint32_t>(std::min<std::uint64_t>(parsed, 4096));
#undef RN_ASSIGN_OFFSET
        }
        continue;
invalid:
        if (error) *error = "invalid value for " + key + " at line " + std::to_string(line_number);
        return false;
    }
    seed_provenance();
    return true;
}

bool OffsetProfile::save_ini(const std::string& path, std::string* error) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (error) *error = "cannot write profile: " + path; return false; }
    out << "[target]\n"
        << "build=" << build << "\npackage=" << package_name << "\nmodule=" << module_name << "\n\n[offsets]\n";
#define RN_WRITE_OFFSET(name) out << #name "=0x" << std::hex << std::uppercase << name << "\n"
    RN_WRITE_OFFSET(gworld); RN_WRITE_OFFSET(world_owning_game_instance); RN_WRITE_OFFSET(world_actor_array);
    RN_WRITE_OFFSET(level_actor_array);
    RN_WRITE_OFFSET(game_instance_local_players); RN_WRITE_OFFSET(local_player_controller);
    RN_WRITE_OFFSET(controller_acknowledged_pawn); RN_WRITE_OFFSET(controller_camera_manager);
    RN_WRITE_OFFSET(controller_firing_state); RN_WRITE_OFFSET(controller_aiming_state);
    RN_WRITE_OFFSET(controller_aim_pitch_delta); RN_WRITE_OFFSET(controller_aim_yaw_delta);
    RN_WRITE_OFFSET(pawn_player_state); RN_WRITE_OFFSET(player_state_team);
    RN_WRITE_OFFSET(player_state_pawn_private);
    RN_WRITE_OFFSET(actor_class_fingerprint); RN_WRITE_OFFSET(actor_player_state); RN_WRITE_OFFSET(actor_root_component);
    RN_WRITE_OFFSET(character_mesh); RN_WRITE_OFFSET(scene_component_location); RN_WRITE_OFFSET(scene_component_velocity);
    RN_WRITE_OFFSET(camera_cache); RN_WRITE_OFFSET(camera_cache_size); RN_WRITE_OFFSET(mesh_bone_array);
    RN_WRITE_OFFSET(mesh_bone_array_fallback); RN_WRITE_OFFSET(mesh_component_to_world); RN_WRITE_OFFSET(mesh_visibility_time);
    RN_WRITE_OFFSET(bone_stride);
#undef RN_WRITE_OFFSET
    out << std::dec << "max_entities=" << max_entities << "\nallow_unverified_fields=" << (allow_unverified_fields ? "true" : "false") << "\n";
    return static_cast<bool>(out);
}

std::vector<std::string> OffsetProfile::validate() const {
    std::vector<std::string> errors;
    if (build.empty()) errors.emplace_back("build is empty");
    if (package_name.empty()) errors.emplace_back("package is empty");
    if (module_name.empty()) errors.emplace_back("module is empty");
    if (gworld == 0 || gworld > 0x400000000ULL) errors.emplace_back("gworld is out of range");
    if (world_owning_game_instance == 0 || game_instance_local_players == 0 ||
        local_player_controller == 0 || controller_camera_manager == 0 || camera_cache == 0)
        errors.emplace_back("verified local-player/camera chain is incomplete");
    if (camera_cache_size != 0x34) errors.emplace_back("camera_cache_size differs from verified donor size 0x34");
    if (max_entities == 0 || max_entities > 4096) errors.emplace_back("max_entities must be 1..4096");
    return errors;
}

}  // namespace rebootnova
