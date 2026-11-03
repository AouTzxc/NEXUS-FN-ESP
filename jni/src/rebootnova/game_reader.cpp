#include "rebootnova/game_reader.h"
#include "rebootnova/runtime_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace rebootnova {
namespace {

struct RemoteArray {
    std::uintptr_t data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct RemoteTransform96 {
    double qx{}, qy{}, qz{}, qw{};
    double tx{}, ty{}, tz{}, translation_pad{};
    double sx{1.0}, sy{1.0}, sz{1.0}, scale_pad{};
};
static_assert(sizeof(RemoteTransform96) == 0x60, "UE5 transform size");

constexpr std::uint64_t kWorldActorFingerprint = 0x020011CBE9746C70ULL;
constexpr std::int32_t kDonorActorCountLimit = 50000;
constexpr std::int32_t kDonorLevelCountLimit = 500;
// Cover more of the UE heap per frame so remote meshes become visible during
// the same encounter instead of waiting for a long cold scan.
constexpr std::size_t kMeshScanChunk = 16U * 1024U * 1024U;
constexpr std::size_t kMeshScanRecoveryChunk = 32U * 1024U * 1024U;
constexpr std::size_t kMeshScanReadBlock = 64U * 1024U;
constexpr std::uintptr_t kMeshScanWindow = 1024ULL * 1024ULL * 1024ULL;
constexpr std::uintptr_t kMeshScanMinMapping = 1024ULL * 1024ULL;
constexpr std::uintptr_t kMeshScanLocalLead = 32ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxDiscoveredMeshes = 512;
constexpr std::uint16_t kMeshCacheMissGrace = 180;
constexpr std::size_t kMeshOwnerProbeSize = 0x400;
// Keep the external reader below a small, predictable per-frame budget. The
// old 16 MiB hot + 4 MiB cold pass made the overlay itself run near 6 FPS.
constexpr std::size_t kActorScanChunk = 2U * 1024U * 1024U;
constexpr std::size_t kActorScanRescanChunk = 512U * 1024U;
constexpr std::size_t kActorColdScanChunk = 256U * 1024U;
constexpr std::uint64_t kActorHotScanStride = 8U;
constexpr std::uint64_t kActorColdScanStride = 64U;
constexpr std::size_t kActorObjectProbeStart = 0x100U;
constexpr std::size_t kActorObjectProbeSize = 0x600U;
constexpr std::size_t kActorMeshProbeBudgetPerFrame = 2U;
constexpr std::uint16_t kActorMeshProbeRetryFrames = 120U;
constexpr std::size_t kActorComponentArrayLimit = 12U;
constexpr std::size_t kActorComponentElementLimit = 64U;
constexpr std::size_t kActorScanReadBlock = 64U * 1024U;
constexpr std::size_t kActorScanMinMapping = 1U * 1024U * 1024U;
constexpr std::uint64_t kPlayerActorFingerprint573 = 0x020011C3E9746C70ULL;
constexpr std::uint64_t kPlayerActorFingerprint575 = 0x020011C3A9746C70ULL;
constexpr auto kRenderFreshness = std::chrono::milliseconds(400);
constexpr auto kMeshMotionFreshness = std::chrono::milliseconds(250);
constexpr float kRenderAdvanceEpsilon = 0.0001F;
// UE LastRenderTime is world seconds. A mesh still on-screen stays within a
// short slack of the local pawn's clock; occluded meshes freeze below it.
constexpr float kRenderTimeSlack = 0.12F;
constexpr float kMeshMotionEpsilon = 0.05F;
constexpr float kMeshMotionTeleport = 5000.0F;
constexpr float kMeshOriginToCapsuleCenter = 75.0F;
constexpr float kRootMeshMaxSeparation = 400.0F;
// Identity notifications live in libc_malloc arenas, but reading large chunks
// from the capture thread competes with entity/bone reads.  A 64 KiB slice is
// enough to cover account_id/account_dn plus the overlap without affecting ESP
// cadence on high-refresh devices.
constexpr std::size_t kPartyIdentityScanChunk = 64U * 1024U;
constexpr std::size_t kPartyIdentityScanLimit = 64U * 1024U * 1024U;
constexpr std::size_t kPartyIdentityOverlap = 1024U;
constexpr auto kPartyIdentityRescanInterval = std::chrono::seconds(30);
constexpr std::size_t kScoreboardScanChunk = 256U * 1024U;
constexpr std::size_t kScoreboardRangeLead = 16U * 1024U * 1024U;
constexpr std::uintptr_t kScoreboardStateLookbehind = 32ULL * 1024ULL * 1024ULL;
constexpr std::uintptr_t kScoreboardStateLookahead = 8ULL * 1024ULL * 1024ULL;
constexpr std::uintptr_t kScoreboardHintWindow = 32ULL * 1024ULL * 1024ULL;
constexpr std::size_t kScoreboardNeighborWindow = 16U * 1024U;
constexpr auto kScoreboardRescanInterval = std::chrono::seconds(5);
constexpr std::size_t kPersistentNameCacheLimit = 4096;

Vec3 transform_position(const RemoteTransform96& local, const RemoteTransform96& component) {
    double x = local.tx * component.sx;
    double y = local.ty * component.sy;
    double z = local.tz * component.sz;
    const double qx = component.qx, qy = component.qy, qz = component.qz, qw = component.qw;
    const double ix = qw * x + qy * z - qz * y;
    const double iy = qw * y + qz * x - qx * z;
    const double iz = qw * z + qx * y - qy * x;
    const double iw = -qx * x - qy * y - qz * z;
    const double rx = ix * qw + iw * -qx + iy * -qz - iz * -qy;
    const double ry = iy * qw + iw * -qy + iz * -qx - ix * -qz;
    const double rz = iz * qw + iw * -qz + ix * -qy - iy * -qx;
    return {static_cast<float>(component.tx + rx), static_cast<float>(component.ty + ry), static_cast<float>(component.tz + rz)};
}

bool plausible_location(const Vec3& value) {
    return value.finite() && std::abs(value.x) < 1.0e8F && std::abs(value.y) < 1.0e8F && std::abs(value.z) < 1.0e8F;
}

bool plausible_component_transform(const RemoteTransform96& value) {
    const double quaternion_norm = value.qx * value.qx + value.qy * value.qy +
                                   value.qz * value.qz + value.qw * value.qw;
    const auto plausible_scale = [](double scale) {
        return std::isfinite(scale) && std::abs(scale) >= 0.01 && std::abs(scale) <= 10.0;
    };
    return std::isfinite(quaternion_norm) && quaternion_norm >= 0.8 && quaternion_norm <= 1.2 &&
           std::isfinite(value.tx) && std::isfinite(value.ty) && std::isfinite(value.tz) &&
           std::abs(value.tx) < 1.0e8 && std::abs(value.ty) < 1.0e8 && std::abs(value.tz) < 1.0e8 &&
           (std::abs(value.tx) + std::abs(value.ty) + std::abs(value.tz)) > 1.0 &&
           plausible_scale(value.sx) && plausible_scale(value.sy) && plausible_scale(value.sz);
}

Vec3 mesh_capsule_center(const RemoteTransform96& component) {
    return {
        static_cast<float>(component.tx),
        static_cast<float>(component.ty),
        static_cast<float>(component.tz) + kMeshOriginToCapsuleCenter,
    };
}

bool align_to_fresh_mesh(EntitySnapshot& entity, const RemoteTransform96& component,
                         bool mesh_fresh) {
    if (!mesh_fresh || !plausible_component_transform(component)) return false;
    const Vec3 visual_center = mesh_capsule_center(component);
    if (!plausible_location(visual_center)) return false;
    entity.location = visual_center;
    entity.visual_mesh_aligned = true;
    return true;
}

bool root_matches_mesh(const std::optional<Vec3d>& root_location,
                       const RemoteTransform96& component) {
    if (!root_location || !root_location->finite()) return false;
    const Vec3 root = root_location->narrow();
    const Vec3 mesh = mesh_capsule_center(component);
    return plausible_location(root) && plausible_location(mesh) &&
           (root - mesh).length() <= kRootMeshMaxSeparation;
}

bool is_player_actor_fingerprint(std::uint64_t fingerprint) {
    return fingerprint == kPlayerActorFingerprint573 ||
           fingerprint == kPlayerActorFingerprint575;
}

// The TeamIndex read width is build-specific; only the offset comes from the
// profile. CL-59343437 stores TeamIndex as one byte at PlayerState+0xF49 and
// 0xff is the engine's no-team sentinel, so an int32 read there would fold the
// three following bytes of an unrelated field into the team value.
int read_player_team(const ProcessMemory& memory, std::uintptr_t player_state,
                     const OffsetProfile& profile) {
    if (profile.player_state_team == 0 || !memory.valid_pointer(player_state)) return -1;
    const auto address = player_state + static_cast<std::uintptr_t>(profile.player_state_team);
    if (profile.build == "59343437") {
        const auto value = memory.read<std::uint8_t>(address);
        return value && *value < 255 ? static_cast<int>(*value) : -1;
    }
    const auto value = memory.read<std::int32_t>(address);
    return value && *value >= 0 ? *value : -1;
}

bool valid_bone_array(const ProcessMemory& memory, const RemoteArray& array) {
    return memory.valid_pointer(array.data) && array.count > 0 &&
           array.count <= 512 && array.capacity >= array.count &&
           array.capacity <= 512;
}

std::optional<RemoteArray> read_bone_array(const ProcessMemory& memory,
                                           std::uintptr_t mesh,
                                           std::uint64_t primary_offset) {
    const std::array<std::uint64_t, 3> offsets{primary_offset, 0x658, 0x660};
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        const auto offset = offsets[index];
        if (offset == 0) continue;
        bool duplicate = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (offsets[prior] == offset) { duplicate = true; break; }
        }
        if (duplicate) continue;
        const auto array = memory.read<RemoteArray>(mesh + offset);
        if (array && valid_bone_array(memory, *array)) return array;
    }
    return std::nullopt;
}

std::optional<std::uintptr_t> read_root_component(const ProcessMemory& memory,
                                                   std::uintptr_t actor,
                                                   std::uint64_t primary_offset) {
    const std::array<std::uint64_t, 3> offsets{primary_offset, 0x158, 0x1B0};
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        const auto offset = offsets[index];
        if (offset == 0) continue;
        bool duplicate = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (offsets[prior] == offset) { duplicate = true; break; }
        }
        if (duplicate) continue;
        const auto root = memory.read<std::uintptr_t>(actor + offset);
        if (root && memory.valid_pointer(*root)) return root;
    }
    return std::nullopt;
}

std::optional<Vec3d> read_scene_location(const ProcessMemory& memory,
                                         std::uintptr_t root,
                                         std::uint64_t primary_offset) {
    const std::array<std::uint64_t, 3> offsets{primary_offset, 0x180, 0x200};
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        const auto offset = offsets[index];
        if (offset == 0) continue;
        bool duplicate = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (offsets[prior] == offset) { duplicate = true; break; }
        }
        if (duplicate) continue;
        const auto location = memory.read<Vec3d>(root + offset);
        if (location && location->finite() && plausible_location(location->narrow()))
            return location;
    }
    return std::nullopt;
}

bool align_to_live_root(EntitySnapshot& entity, const RemoteTransform96& component,
                        const std::optional<Vec3d>& root_location) {
    if (!root_location || !root_location->finite()) return false;
    const Vec3 live_center = root_location->narrow();
    if (!plausible_location(live_center)) return false;
    const Vec3 cached_center = mesh_capsule_center(component);
    const Vec3 correction = live_center - cached_center;
    entity.location = live_center;
    entity.visual_mesh_aligned = true;
    for (auto& bone : entity.bones) {
        const bool populated = std::abs(bone.x) + std::abs(bone.y) + std::abs(bone.z) > 1.0F;
        if (populated && plausible_location(bone)) bone = bone + correction;
    }
    return true;
}

bool valid_render_time_sample(float value) {
    return std::isfinite(value) && value >= -10000.0F && value <= 1.0e7F;
}

bool currently_rendered(float value) {
    return valid_render_time_sample(value) && value > 0.0F;
}

}  // namespace

GameReader::GameReader(ProcessMemory& memory, OffsetProfile& profile)
    : memory_(memory), profile_(profile) {}

void GameReader::set_profile(const OffsetProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool mesh_layout_changed =
        profile_.character_mesh != profile.character_mesh ||
        profile_.mesh_bone_array != profile.mesh_bone_array ||
        profile_.mesh_bone_array_fallback != profile.mesh_bone_array_fallback ||
        profile_.mesh_component_to_world != profile.mesh_component_to_world ||
        profile_.bone_stride != profile.bone_stride;
    const bool visibility_layout_changed =
        profile_.mesh_visibility_time != profile.mesh_visibility_time;
    profile_ = profile;
    if (mesh_layout_changed) {
        actor_mesh_cache_.clear();
        actor_mesh_probe_misses_.clear();
        discovered_meshes_.clear();
        mesh_cache_misses_.clear();
    }
    if (visibility_layout_changed) actor_temporal_.clear();
}

std::string platform_from_text(const std::string& value) {
    std::string text = value;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // EOS/UE platform fields are not consistent across modes: some builds
    // expose the long name while others store the short external-account
    // token. Match exact short tokens only; never infer from opaque ID bytes.
    if (text == "and" || text == "android") return "Android";
    if (text == "ios" || text == "iphone" || text == "ipad") return "iOS";
    if (text == "psn" || text == "ps" || text == "ps4" || text == "ps5") return "PlayStation";
    if (text == "xbl" || text == "xbox") return "Xbox";
    if (text == "swt" || text == "switch" || text == "nintendo") return "Switch";
    if (text == "win" || text == "pc" || text == "win64" || text == "windows") return "PC";
    if (text.find("android") != std::string::npos) return "Android";
    if (text.find("ios") != std::string::npos || text.find("iphone") != std::string::npos ||
        text.find("ipad") != std::string::npos) return "iOS";
    if (text.find("playstation") != std::string::npos || text == "ps" || text.find("ps4") != std::string::npos ||
        text.find("ps5") != std::string::npos) return "PlayStation";
    if (text.find("xbox") != std::string::npos) return "Xbox";
    if (text.find("switch") != std::string::npos || text.find("nintendo") != std::string::npos) return "Switch";
    if (text.find("windows") != std::string::npos || text == "pc" || text.find("win64") != std::string::npos) return "PC";
    return {};
}

std::string utf16_to_utf8(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    const auto append = [&out](std::uint32_t cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0x10ffff) {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    };
    for (std::size_t i = 0; i + 1 < bytes.size();) {
        const std::uint16_t unit = static_cast<std::uint16_t>(bytes[i]) |
                                   (static_cast<std::uint16_t>(bytes[i + 1]) << 8);
        i += 2;
        if (unit == 0) break;
        std::uint32_t cp = unit;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (i + 1 >= bytes.size()) return {};
            const std::uint16_t low = static_cast<std::uint16_t>(bytes[i]) |
                                      (static_cast<std::uint16_t>(bytes[i + 1]) << 8);
            if (low < 0xdc00 || low > 0xdfff) return {};
            i += 2;
            cp = 0x10000u + ((static_cast<std::uint32_t>(unit) - 0xd800u) << 10) +
                 (static_cast<std::uint32_t>(low) - 0xdc00u);
        }
        if ((cp >= 0xdc00 && cp <= 0xdfff) || cp == 0xfffdu) return {};
        append(cp);
    }
    return out;
}

bool plausible_scoreboard_name(const std::string& value) {
    if (value.size() < 2 || value.size() > 96) return false;
    bool visible = false;
    for (const unsigned char byte : value) {
        if (byte < 0x20 || byte == 0x7f) return false;
        visible = visible || byte >= 0x21;
    }
    return visible;
}

bool localized_name_fallback(const std::string& value) {
    if (!plausible_scoreboard_name(value)) return false;
    return std::any_of(value.begin(), value.end(), [](unsigned char byte) {
        return byte >= 0x80;
    });
}

std::string read_localized_player_name(ProcessMemory& memory,
                                       std::uintptr_t player_state) {
    if (!memory.valid_pointer(player_state)) return {};
    // Human aliases in CL-57566230 community modes are opaque ASCII strings.
    // Localized AI names still use these replicated UTF-16 FString slots, so
    // accept only non-ASCII text here and let verified mappings override it.
    for (const std::uintptr_t offset : {0x300ULL, 0x2F0ULL}) {
        const auto data = memory.read<std::uintptr_t>(player_state + offset);
        const auto count = memory.read<std::int32_t>(player_state + offset + 8);
        const auto capacity = memory.read<std::int32_t>(player_state + offset + 12);
        if (!data || !count || !capacity || !memory.valid_pointer(*data) ||
            *count < 2 || *count > 64 || *capacity < *count || *capacity > 256) continue;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(*count) * 2U);
        if (!memory.read_bytes(*data, bytes.data(), bytes.size())) continue;
        auto name = utf16_to_utf8(bytes);
        if (localized_name_fallback(name)) return name;
    }
    return {};
}

std::string read_scoreboard_name(ProcessMemory& memory, std::uintptr_t row) {
    const auto history = memory.read<std::uintptr_t>(row);
    const auto tag = memory.read<std::uint32_t>(row + 0x08);
    if (!history || !tag || !memory.valid_pointer(*history) || *tag != 0x12U) return {};
    const auto data = memory.read<std::uintptr_t>(*history + 0x20);
    const auto count = memory.read<std::int32_t>(*history + 0x28);
    if (!data || !count || !memory.valid_pointer(*data) || *count < 2 || *count > 96) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(*count) * 2U);
    if (!memory.read_bytes(*data, bytes.data(), bytes.size())) return {};
    auto name = utf16_to_utf8(bytes);
    return plausible_scoreboard_name(name) ? name : std::string{};
}

bool valid_scoreboard_row(ProcessMemory& memory, std::uintptr_t row,
                          const std::unordered_set<std::uintptr_t>& live_states,
                          std::uintptr_t& player_state, std::string& name) {
    const auto team_color = memory.read<std::int32_t>(row + 0x10);
    const auto values = memory.read<RemoteArray>(row + 0x18);
    const auto state = memory.read<std::uintptr_t>(row + 0x30);
    if (!team_color || !values || !state || *team_color < -1 || *team_color > 255 ||
        values->count < 0 || values->count > 32 || values->capacity < values->count ||
        values->capacity > 64 || live_states.find(*state) == live_states.end()) return false;
    name = read_scoreboard_name(memory, row);
    if (name.empty()) return false;
    player_state = *state;
    return true;
}

std::string read_player_platform(ProcessMemory& memory, std::uintptr_t player_state) {
    if (!memory.valid_pointer(player_state)) return "Unknown";
    // UE FString candidates in PlayerState are pointer/length/capacity triples.
    // Only accept explicit platform words; opaque UniqueNetId bytes are never
    // guessed as a device class.
    std::array<std::uint8_t, 0x700> state{};
    if (!memory.read_bytes(player_state, state.data(), state.size())) return "Unknown";
    for (std::uintptr_t offset = 0; offset + 16 <= 0x700; offset += 8) {
        std::uintptr_t ptr{};
        std::int32_t length{};
        std::int32_t capacity{};
        std::memcpy(&ptr, state.data() + offset, sizeof(ptr));
        std::memcpy(&length, state.data() + offset + 8, sizeof(length));
        std::memcpy(&capacity, state.data() + offset + 12, sizeof(capacity));
        if (length <= 0 || length > 64 || capacity < length || capacity > 256 ||
            !memory.valid_pointer(ptr)) continue;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length) * 2U);
        if (!memory.read_bytes(ptr, bytes.data(), bytes.size())) continue;
        std::string text(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::size_t>(length));
        if (const auto terminator = text.find('\0'); terminator != std::string::npos)
            text.resize(terminator);
        if (const auto platform = platform_from_text(text); !platform.empty()) return platform;
        // FString uses UTF-16 code units on Android. Try the same candidate
        // as a wide string without treating arbitrary opaque bytes as text.
        const auto narrow = utf16_to_utf8(bytes);
        if (const auto platform = platform_from_text(narrow); !platform.empty()) return platform;
    }
    return "Unknown";
}

std::string read_player_id(ProcessMemory& memory, std::uintptr_t player_state) {
    if (!memory.valid_pointer(player_state)) return {};
    // CL-57566230: PlayerState+0x278 owns the replicated Epic account-id
    // wrapper. Its +0x10 FString contains 32 hexadecimal characters plus NUL.
    if (const auto identity = memory.read<std::uintptr_t>(player_state + 0x278);
        identity && memory.valid_pointer(*identity)) {
        const auto data = memory.read<std::uintptr_t>(*identity + 0x10);
        const auto count = memory.read<std::int32_t>(*identity + 0x18);
        const auto capacity = memory.read<std::int32_t>(*identity + 0x1C);
        if (data && count && capacity && memory.valid_pointer(*data) &&
            *count >= 32 && *count <= 33 && *capacity >= *count && *capacity <= 64) {
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(*count) * 2U);
            if (memory.read_bytes(*data, bytes.data(), bytes.size())) {
                std::string id = utf16_to_utf8(bytes);
                if (id.size() == 32 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
                        return std::isxdigit(c) != 0;
                    })) {
                    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    return id;
                }
            }
        }
    }
    // Older fallback: expose opaque UniqueNetId bytes as hex when the Epic
    // account wrapper is unavailable.
    const auto data = memory.read<std::uintptr_t>(player_state + 0x2C8);
    const auto count = memory.read<std::int32_t>(player_state + 0x2D0);
    const auto capacity = memory.read<std::int32_t>(player_state + 0x2D4);
    if (!data || !count || !capacity || *count <= 0 || *count > 64 || *capacity < *count ||
        *capacity > 256 || !memory.valid_pointer(*data)) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(*count));
    if (!memory.read_bytes(*data, bytes.data(), bytes.size())) return {};
    std::string result;
    result.reserve(bytes.size() * 2);
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const auto byte : bytes) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 0x0F]); }
    return result;
}

bool is_hex_account_id(std::string_view value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

std::optional<std::string> parse_json_string(std::string_view text, std::size_t value_start) {
    std::string out;
    out.reserve(32);
    const auto hex_nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const auto read_hex_quad = [&text, &hex_nibble](std::size_t start)
        -> std::optional<std::uint16_t> {
        if (start + 4 > text.size()) return std::nullopt;
        std::uint16_t value{};
        for (std::size_t offset = 0; offset < 4; ++offset) {
            const int nibble = hex_nibble(text[start + offset]);
            if (nibble < 0) return std::nullopt;
            value = static_cast<std::uint16_t>((value << 4) | nibble);
        }
        return value;
    };
    const auto append_utf8 = [&out](std::uint32_t cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    };
    for (std::size_t i = value_start; i < text.size() && out.size() <= 96; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"') return out.empty() ? std::nullopt : std::optional<std::string>{out};
        if (c != '\\') {
            if (c < 0x20) return std::nullopt;
            out.push_back(static_cast<char>(c));
            continue;
        }
        if (++i >= text.size()) return std::nullopt;
        switch (text[i]) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                const auto high = read_hex_quad(i + 1);
                if (!high) return std::nullopt;
                i += 4;
                std::uint32_t cp = *high;
                if (*high >= 0xd800 && *high <= 0xdbff) {
                    if (i + 6 >= text.size() || text[i + 1] != '\\' || text[i + 2] != 'u')
                        return std::nullopt;
                    const auto low = read_hex_quad(i + 3);
                    if (!low || *low < 0xdc00 || *low > 0xdfff) return std::nullopt;
                    cp = 0x10000u + ((static_cast<std::uint32_t>(*high) - 0xd800u) << 10) +
                         (static_cast<std::uint32_t>(*low) - 0xdc00u);
                    i += 6;
                } else if (*high >= 0xdc00 && *high <= 0xdfff) {
                    return std::nullopt;
                }
                append_utf8(cp);
                break;
            }
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

void parse_party_identity_payload(std::string_view text,
                                  std::unordered_map<std::string, std::string>& names) {
    constexpr std::string_view kAccountKey{"\"account_id\":\""};
    constexpr std::string_view kDisplayKey{"\"account_dn\":\""};
    std::size_t cursor{};
    while ((cursor = text.find(kAccountKey, cursor)) != std::string_view::npos) {
        const std::size_t id_start = cursor + kAccountKey.size();
        if (id_start + 32 > text.size()) break;
        std::string id{text.substr(id_start, 32)};
        if (!is_hex_account_id(id)) { cursor = id_start; continue; }
        const std::size_t search_end = std::min(text.size(), id_start + 1024U);
        const std::size_t display_key = text.find(kDisplayKey, id_start + 32);
        if (display_key == std::string_view::npos || display_key >= search_end) {
            cursor = id_start + 32;
            continue;
        }
        const auto display = parse_json_string(text, display_key + kDisplayKey.size());
        if (display && !display->empty()) {
            std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            names[id] = *display;
        }
        cursor = id_start + 32;
    }
}

void GameReader::load_persistent_name_cache() {}

bool GameReader::remember_persistent_name(std::string_view account_id,
                                          std::string_view display_name) {
    if (!is_hex_account_id(account_id) || !plausible_scoreboard_name(std::string(display_name)) ||
        display_name.find('\t') != std::string_view::npos ||
        display_name.find('\n') != std::string_view::npos ||
        display_name.find('\r') != std::string_view::npos) return false;
    std::string normalized_id{account_id};
    std::transform(normalized_id.begin(), normalized_id.end(), normalized_id.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto existing = persistent_account_display_names_.find(normalized_id);
    if (existing != persistent_account_display_names_.end() &&
        existing->second == display_name) return false;
    if (existing == persistent_account_display_names_.end() &&
        persistent_account_display_names_.size() >= kPersistentNameCacheLimit) return false;
    persistent_account_display_names_[std::move(normalized_id)] = std::string(display_name);
    return true;
}

void GameReader::persist_name_cache() const {}

void GameReader::remember_external_names(
    const std::vector<std::pair<std::string, std::string>>& names) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t changed{};
    for (const auto& [account_id, display_name] : names) {
        if (remember_persistent_name(account_id, display_name)) ++changed;
    }
    if (changed == 0) return;
    persist_name_cache();
    runtime::log("online name cache saved added=%zu total=%zu", changed,
                 persistent_account_display_names_.size());
}

void GameReader::scan_party_identity_cache() {
    const auto now = std::chrono::steady_clock::now();
    if (party_identity_scan_ranges_.empty()) {
        if (next_party_identity_scan_.time_since_epoch().count() != 0 &&
            now < next_party_identity_scan_) return;
        for (const auto& range : memory_.readable_mappings()) {
            const std::size_t size = range.end > range.start
                ? static_cast<std::size_t>(range.end - range.start) : 0;
            if (size == 0 || size > 32U * 1024U * 1024U ||
                range.path.find("libc_malloc") == std::string::npos) continue;
            party_identity_scan_ranges_.push_back(range);
        }
        // Keep process-map order.  On CL-57566230 the live party notification
        // arena precedes dozens of same-sized allocator arenas.  Sorting by
        // size made the 64 MiB scan cap nondeterministic and often skipped the
        // arena containing account_id/account_dn entirely.
        std::sort(party_identity_scan_ranges_.begin(), party_identity_scan_ranges_.end(),
                  [](const MemoryRange& a, const MemoryRange& b) {
                      return a.start < b.start;
                  });
        party_identity_scan_range_index_ = 0;
        party_identity_scan_cursor_ = party_identity_scan_ranges_.empty()
            ? 0 : party_identity_scan_ranges_.front().start;
        party_identity_scan_bytes_ = 0;
        if (party_identity_scan_ranges_.empty()) {
            next_party_identity_scan_ = now + kPartyIdentityRescanInterval;
            return;
        }
    }

    while (party_identity_scan_range_index_ < party_identity_scan_ranges_.size()) {
        const auto& range = party_identity_scan_ranges_[party_identity_scan_range_index_];
        if (party_identity_scan_cursor_ < range.start) party_identity_scan_cursor_ = range.start;
        if (party_identity_scan_cursor_ >= range.end) {
            ++party_identity_scan_range_index_;
            if (party_identity_scan_range_index_ < party_identity_scan_ranges_.size())
                party_identity_scan_cursor_ = party_identity_scan_ranges_[party_identity_scan_range_index_].start;
            continue;
        }
        const std::size_t remaining = static_cast<std::size_t>(range.end - party_identity_scan_cursor_);
        const std::size_t size = std::min(remaining, kPartyIdentityScanChunk + kPartyIdentityOverlap);
        std::vector<char> bytes(size);
        if (memory_.read_bytes(party_identity_scan_cursor_, bytes.data(), bytes.size()))
            parse_party_identity_payload(std::string_view(bytes.data(), bytes.size()),
                                         account_display_names_);
        const std::size_t advance = std::min(remaining, kPartyIdentityScanChunk);
        party_identity_scan_cursor_ += advance;
        party_identity_scan_bytes_ += advance;
        break;
    }
    if (party_identity_scan_range_index_ >= party_identity_scan_ranges_.size() ||
        party_identity_scan_bytes_ >= kPartyIdentityScanLimit) {
        party_identity_scan_ranges_.clear();
        party_identity_scan_range_index_ = 0;
        party_identity_scan_cursor_ = 0;
        party_identity_scan_bytes_ = 0;
        next_party_identity_scan_ = now + kPartyIdentityRescanInterval;
    }
}

void GameReader::scan_scoreboard_identity_cache(const LocalSnapshot& local,
                                                std::vector<EntitySnapshot>& entities) {
    std::unordered_set<std::uintptr_t> live_states;
    if (memory_.valid_pointer(local.player_state)) live_states.insert(local.player_state);
    for (const auto& entity : entities) {
        if (memory_.valid_pointer(entity.player_state)) live_states.insert(entity.player_state);
    }
    if (live_states.empty()) return;
    bool persistent_cache_changed = false;
    const auto remember_scoreboard_name = [this](std::uintptr_t state,
                                                  std::string_view name) {
        std::string account_id;
        if (const auto identity = player_identity_cache_.find(state);
            identity != player_identity_cache_.end()) account_id = identity->second.player_id;
        if (account_id.empty()) account_id = read_player_id(memory_, state);
        return remember_persistent_name(account_id, name);
    };

    const auto now = std::chrono::steady_clock::now();
    if (scoreboard_scan_ranges_.empty()) {
        if (next_scoreboard_scan_.time_since_epoch().count() != 0 &&
            now < next_scoreboard_scan_) return;
        const auto mappings = memory_.readable_mappings();
        const auto eligible = [](const MemoryRange& range) {
            const auto size = range.end > range.start ? range.end - range.start : 0;
            return range.writable && !range.executable && range.private_mapping &&
                   range.path.empty() && size >= 1024U * 1024U &&
                   range.end > 0x6D00000000ULL && range.start < 0x6F20000000ULL;
        };
        const auto add_range = [this](const MemoryRange& mapping,
                                      std::uintptr_t start, std::uintptr_t end) {
            start = std::max(start, mapping.start);
            end = std::min(end, mapping.end);
            if (end <= start) return;
            for (const auto& existing : scoreboard_scan_ranges_) {
                if (existing.start == start && existing.end == end) return;
            }
            auto range = mapping;
            range.start = start;
            range.end = end;
            scoreboard_scan_ranges_.push_back(std::move(range));
        };

        // A scoreboard array normally remains in the same arena while the
        // process changes maps. Revisit the last hit first on the next UWorld.
        if (scoreboard_scan_hint_ != 0) {
            for (const auto& mapping : mappings) {
                if (!eligible(mapping) || scoreboard_scan_hint_ < mapping.start ||
                    scoreboard_scan_hint_ >= mapping.end) continue;
                const auto begin = scoreboard_scan_hint_ > kScoreboardHintWindow
                    ? scoreboard_scan_hint_ - kScoreboardHintWindow : mapping.start;
                add_range(mapping, begin, scoreboard_scan_hint_ + kScoreboardHintWindow);
                break;
            }
        }

        // Scoreboard records and PlayerStates are allocated by the same UE
        // heap. In CL-57566230 the row array can sit hundreds of MiB past the
        // mapping start, so a mapping-prefix scan misses it. Search around
        // every live state, ordered by address, before the cold fallback.
        std::vector<std::uintptr_t> ordered_states(live_states.begin(), live_states.end());
        std::sort(ordered_states.begin(), ordered_states.end());
        for (const auto state : ordered_states) {
            for (const auto& mapping : mappings) {
                if (!eligible(mapping) || state < mapping.start || state >= mapping.end) continue;
                const auto begin = state > kScoreboardStateLookbehind
                    ? state - kScoreboardStateLookbehind : mapping.start;
                const auto end = state <= std::numeric_limits<std::uintptr_t>::max() -
                                           kScoreboardStateLookahead
                    ? state + kScoreboardStateLookahead : mapping.end;
                add_range(mapping, begin, end);
                break;
            }
        }
        for (const auto& mapping : mappings) {
            if (!eligible(mapping)) continue;
            add_range(mapping, mapping.start,
                      mapping.start + std::min<std::uintptr_t>(
                          mapping.end - mapping.start, kScoreboardRangeLead));
        }
        scoreboard_scan_range_index_ = 0;
        scoreboard_scan_cursor_ = scoreboard_scan_ranges_.empty()
            ? 0 : scoreboard_scan_ranges_.front().start;
    }

    while (scoreboard_scan_range_index_ < scoreboard_scan_ranges_.size()) {
        const auto& range = scoreboard_scan_ranges_[scoreboard_scan_range_index_];
        if (scoreboard_scan_cursor_ < range.start) scoreboard_scan_cursor_ = range.start;
        if (scoreboard_scan_cursor_ >= range.end) {
            ++scoreboard_scan_range_index_;
            if (scoreboard_scan_range_index_ < scoreboard_scan_ranges_.size())
                scoreboard_scan_cursor_ = scoreboard_scan_ranges_[scoreboard_scan_range_index_].start;
            continue;
        }
        const auto remaining = static_cast<std::size_t>(range.end - scoreboard_scan_cursor_);
        const auto size = std::min(remaining, kScoreboardScanChunk);
        std::vector<std::uint8_t> bytes(size);
        if (memory_.read_bytes(scoreboard_scan_cursor_, bytes.data(), bytes.size())) {
            for (std::size_t offset = 0; offset + sizeof(std::uintptr_t) <= bytes.size();
                 offset += sizeof(std::uintptr_t)) {
                std::uintptr_t value{};
                std::memcpy(&value, bytes.data() + offset, sizeof(value));
                if (live_states.find(value) == live_states.end() || offset < 0x30) continue;
                const auto row = scoreboard_scan_cursor_ + offset - 0x30;
                std::uintptr_t state{};
                std::string name;
                if (!valid_scoreboard_row(memory_, row, live_states, state, name)) continue;
                if (const auto existing = scoreboard_display_names_.find(state);
                    existing == scoreboard_display_names_.end() || existing->second != name) {
                    runtime::log("scoreboard name=%s ps=0x%llX row=0x%llX",
                                 name.c_str(), static_cast<unsigned long long>(state),
                                 static_cast<unsigned long long>(row));
                }
                persistent_cache_changed = remember_scoreboard_name(state, name) ||
                                           persistent_cache_changed;
                scoreboard_display_names_[state] = std::move(name);
                scoreboard_scan_hint_ = row;

                const auto window_begin = row > kScoreboardNeighborWindow
                    ? row - kScoreboardNeighborWindow : row;
                const auto window_end = row + kScoreboardNeighborWindow;
                // Scoreboard rows in CL-57566230 are 0x40-aligned records;
                // scanning that stride avoids thousands of remote syscalls.
                const auto aligned_begin = row - std::min<std::uintptr_t>(
                    row - window_begin, ((row - window_begin) / 0x40U) * 0x40U);
                for (auto candidate = aligned_begin; candidate + 0x38 <= window_end;
                     candidate += 0x40U) {
                    if (candidate == row) continue;
                    std::uintptr_t neighbor_state{};
                    std::string neighbor_name;
                    if (valid_scoreboard_row(memory_, candidate, live_states,
                                             neighbor_state, neighbor_name)) {
                        if (const auto existing = scoreboard_display_names_.find(neighbor_state);
                            existing == scoreboard_display_names_.end() ||
                            existing->second != neighbor_name) {
                            runtime::log("scoreboard name=%s ps=0x%llX row=0x%llX",
                                         neighbor_name.c_str(),
                                         static_cast<unsigned long long>(neighbor_state),
                                         static_cast<unsigned long long>(candidate));
                        }
                        persistent_cache_changed =
                            remember_scoreboard_name(neighbor_state, neighbor_name) ||
                            persistent_cache_changed;
                        scoreboard_display_names_[neighbor_state] = std::move(neighbor_name);
                    }
                }
            }
        }
        if (!scoreboard_scan_ranges_.empty()) scoreboard_scan_cursor_ += size;
        break;
    }
    if (scoreboard_scan_range_index_ >= scoreboard_scan_ranges_.size() &&
        !scoreboard_scan_ranges_.empty()) {
        scoreboard_scan_ranges_.clear();
        scoreboard_scan_range_index_ = 0;
        scoreboard_scan_cursor_ = 0;
        next_scoreboard_scan_ = now + kScoreboardRescanInterval;
    }
    if (persistent_cache_changed) persist_name_cache();

    for (auto& entity : entities) {
        const auto display = scoreboard_display_names_.find(entity.player_state);
        if (display == scoreboard_display_names_.end()) continue;
        entity.player_name = display->second;
        player_identity_cache_[entity.player_state].player_name = display->second;
    }
}

void GameReader::populate_player_identity(EntitySnapshot& entity) {
    if (!entity.player_state) return;
    const auto now = std::chrono::steady_clock::now();
    auto& cached = player_identity_cache_[entity.player_state];
    const bool name_was_empty = cached.player_name.empty();
    const auto basic_refresh_after = cached.player_name.empty()
        ? std::chrono::seconds(5) : std::chrono::seconds(30);
    if (cached.basic_refreshed.time_since_epoch().count() == 0 ||
        now - cached.basic_refreshed >= basic_refresh_after) {
        cached.player_id = read_player_id(memory_, entity.player_state);
        cached.player_name = read_localized_player_name(memory_, entity.player_state);
        cached.basic_refreshed = now;
    }
    if (const auto display = persistent_account_display_names_.find(cached.player_id);
        display != persistent_account_display_names_.end()) cached.player_name = display->second;
    if (const auto display = account_display_names_.find(cached.player_id);
        display != account_display_names_.end()) cached.player_name = display->second;
    if (const auto display = scoreboard_display_names_.find(entity.player_state);
        display != scoreboard_display_names_.end()) cached.player_name = display->second;
    if (name_was_empty && !cached.player_name.empty()) {
        runtime::log("identity resolved ps=0x%llX account=%s name=%s",
                     static_cast<unsigned long long>(entity.player_state),
                     cached.player_id.c_str(), cached.player_name.c_str());
    }
    const auto platform_refresh_after = cached.platform == "Unknown"
        ? std::chrono::seconds(8) : std::chrono::seconds(30);
    const bool platform_due = cached.platform_refreshed.time_since_epoch().count() == 0 ||
        now - cached.platform_refreshed >= platform_refresh_after;
    const bool platform_slot = next_platform_probe_.time_since_epoch().count() == 0 ||
        now >= next_platform_probe_;
    if (platform_due && platform_slot) {
        cached.platform = read_player_platform(memory_, entity.player_state);
        cached.platform_refreshed = now;
        next_platform_probe_ = now + std::chrono::milliseconds(50);
    }
    entity.player_name = cached.player_name;
    entity.player_id = cached.player_id;
    entity.platform = cached.platform;
}

bool GameReader::validate_skeletal_mesh(std::uintptr_t mesh) const {
    if (!memory_.valid_pointer(mesh)) return false;
    const auto bones = read_bone_array(memory_, mesh, profile_.mesh_bone_array);
    if (!bones) return false;
    const auto component = memory_.read<RemoteTransform96>(
        mesh + profile_.mesh_component_to_world);
    // Shape validation is hot path. Bone transforms are read once by the
    // caller; probing all eight here and then reading them again is costly.
    return component && plausible_component_transform(*component);
}

std::uintptr_t GameReader::resolve_actor_mesh(std::uintptr_t actor,
                                              std::uintptr_t local_mesh,
                                              std::size_t& probe_budget,
                                              EntityDiagnostics& diagnostics) {
    const auto cached = actor_mesh_cache_.find(actor);
    if (cached != actor_mesh_cache_.end()) {
        if (cached->second != local_mesh && validate_skeletal_mesh(cached->second))
            return cached->second;
        actor_mesh_cache_.erase(cached);
    }
    auto& retry = actor_mesh_probe_misses_[actor];
    if (retry > 0) {
        --retry;
        return 0;
    }
    if (!probe_budget) return 0;
    --probe_budget;
    ++diagnostics.actor_mesh_scans;

    std::array<std::uint8_t, kActorObjectProbeSize> object{};
    if (!memory_.read_bytes(actor + kActorObjectProbeStart, object.data(), object.size())) {
        retry = kActorMeshProbeRetryFrames;
        return 0;
    }
    std::unordered_set<std::uintptr_t> tested;
    std::uintptr_t mesh_hit{};
    auto inspect_pointer = [&](std::uintptr_t candidate) {
        if (!memory_.valid_pointer(candidate) || !tested.insert(candidate).second) return;
        if (!mesh_hit && candidate != local_mesh && validate_skeletal_mesh(candidate)) {
            mesh_hit = candidate;
            ++diagnostics.actor_mesh_hits;
        }
    };
    for (std::size_t offset = 0; offset + sizeof(std::uintptr_t) <= object.size();
         offset += alignof(void*)) {
        std::uintptr_t candidate{};
        std::memcpy(&candidate, object.data() + offset, sizeof(candidate));
        inspect_pointer(candidate);
    }
    // UE Actor-owned component collections are TArrays rather than guaranteed
    // direct fields. Discover bounded pointer arrays from the same object
    // snapshot and inspect their elements without scanning the arena.
    std::size_t arrays_tested{};
    for (std::size_t offset = 0;
         offset + sizeof(RemoteArray) <= object.size() &&
         arrays_tested < kActorComponentArrayLimit;
         offset += alignof(void*)) {
        RemoteArray array{};
        std::memcpy(&array, object.data() + offset, sizeof(array));
        if (!memory_.valid_pointer(array.data) || array.count <= 0 ||
            array.count > static_cast<std::int32_t>(kActorComponentElementLimit) ||
            array.capacity < array.count ||
            array.capacity > static_cast<std::int32_t>(kActorComponentElementLimit * 2U))
            continue;
        ++arrays_tested;
        ++diagnostics.actor_component_arrays;
        std::array<std::uintptr_t, kActorComponentElementLimit> elements{};
        const std::size_t count = static_cast<std::size_t>(array.count);
        if (!memory_.read_bytes(array.data, elements.data(),
                                count * sizeof(elements.front())))
            continue;
        for (std::size_t index = 0; index < count; ++index)
            inspect_pointer(elements[index]);
    }
    if (mesh_hit) {
        actor_mesh_cache_[actor] = mesh_hit;
        actor_mesh_probe_misses_.erase(actor);
        return mesh_hit;
    }
    retry = kActorMeshProbeRetryFrames;
    return 0;
}

bool GameReader::ensure_attached(std::string& error) {
    const auto now = std::chrono::steady_clock::now();
    const bool probe_due = next_process_probe_.time_since_epoch().count() == 0 || now >= next_process_probe_;
    if (probe_due) {
        // The target can be relaunched with a new PID while the helper keeps
        // running. Poll the package periodically instead of waiting for the
        // old /proc entry to disappear from the frame path.
        next_process_probe_ = now + std::chrono::milliseconds(500);
        const pid_t before = memory_.pid();
        if (!memory_.attach_by_package(profile_.package_name)) {
            // A transient /proc visibility miss must not discard a still-live
            // session during travel. Retry on the next probe while continuing
            // to read the current process.
            if (memory_.alive() && memory_.module_base()) return true;
            error = memory_.last_error();
            return false;
        }
        if (before != memory_.pid()) reset_process_state(memory_.pid());

        const std::uintptr_t before_module = memory_.module_base();
        const std::vector<std::string> candidates{
            profile_.module_name,
            "libUnrealEngine.so",
            "libUnreal.so",
            "libUE4.so",
        };
        if (!memory_.refresh_module_candidates(candidates)) {
            error = memory_.last_error();
            return false;
        }
        if (before_module != 0 && before_module != memory_.module_base())
            reset_process_state(memory_.pid(), memory_.module_base());
        observed_module_base_ = memory_.module_base();
    } else if (!memory_.alive()) {
        error = memory_.last_error().empty() ? "target process is not alive" : memory_.last_error();
        return false;
    }
    if (!memory_.module_base()) {
        const std::vector<std::string> candidates{
            profile_.module_name,
            "libUnrealEngine.so",
            "libUnreal.so",
            "libUE4.so",
        };
        if (!memory_.refresh_module_candidates(candidates)) {
            error = memory_.last_error();
            return false;
        }
        runtime::log("attached pid=%d module selected=%s base=0x%llX",
                     static_cast<int>(memory_.pid()), memory_.module_name().c_str(),
                     static_cast<unsigned long long>(memory_.module_base()));
        observed_module_base_ = memory_.module_base();
    }
    return true;
}

void GameReader::reset_process_state(pid_t pid, std::uintptr_t module_base) {
    next_diagnostic_probe_={};cached_local_probe_={};cached_verified_probe_={};
    observed_pid_ = pid;
    observed_module_base_ = module_base;
    observed_world_ = 0;
    local_read_failures_ = 0;
    mesh_scan_owner_ = 0;
    mesh_scan_cursor_ = 0;
    mesh_scan_end_ = 0;
    mesh_scan_ranges_.clear();
    mesh_scan_range_index_ = 0;
    discovered_meshes_.clear();
    mesh_cache_misses_.clear();
    actor_scan_pid_ = pid;
    actor_scan_owner_ = 0;
    actor_scan_ranges_.clear();
    actor_hot_ranges_.clear();
    actor_scan_range_index_ = 0;
    actor_scan_cursor_ = 0;
    actor_cold_range_index_ = 0;
    actor_cold_cursor_ = 0;
    actor_scan_complete_ = false;
    discovered_player_actors_.clear();
    actor_cache_misses_.clear();
    actor_mesh_cache_.clear();
    actor_mesh_probe_misses_.clear();
    actor_temporal_.clear();
    player_identity_cache_.clear();
    scoreboard_display_names_.clear();
    scoreboard_scan_ranges_.clear();
    scoreboard_scan_range_index_ = 0;
    scoreboard_scan_cursor_ = 0;
    scoreboard_scan_hint_ = 0;
    next_scoreboard_scan_ = {};
    account_display_names_.clear();
    party_identity_scan_ranges_.clear();
    party_identity_scan_range_index_ = 0;
    party_identity_scan_cursor_ = 0;
    party_identity_scan_bytes_ = 0;
    next_party_identity_scan_ = {};
    next_platform_probe_ = {};
    mesh_signature_tests_ = 0;
    mesh_signature_rejected_ = 0;
    mesh_scan_blocks_ok_ = 0;
    mesh_scan_blocks_failed_ = 0;
    mesh_owner_offset_ = 0;
    runtime::log("target process changed; reader state reset pid=%d", static_cast<int>(pid));
}

void GameReader::reset_world_state(std::uintptr_t world) {
    next_diagnostic_probe_={};cached_local_probe_={};cached_verified_probe_={};
    observed_world_ = world;
    mesh_scan_owner_ = 0;
    mesh_scan_cursor_ = 0;
    mesh_scan_end_ = 0;
    mesh_scan_ranges_.clear();
    mesh_scan_range_index_ = 0;
    discovered_meshes_.clear();
    mesh_cache_misses_.clear();
    actor_scan_owner_ = 0;
    actor_scan_ranges_.clear();
    actor_hot_ranges_.clear();
    actor_scan_range_index_ = 0;
    actor_scan_cursor_ = 0;
    actor_cold_range_index_ = 0;
    actor_cold_cursor_ = 0;
    actor_scan_complete_ = false;
    discovered_player_actors_.clear();
    actor_cache_misses_.clear();
    actor_mesh_cache_.clear();
    actor_mesh_probe_misses_.clear();
    actor_temporal_.clear();
    player_identity_cache_.clear();
    scoreboard_display_names_.clear();
    scoreboard_scan_ranges_.clear();
    scoreboard_scan_range_index_ = 0;
    scoreboard_scan_cursor_ = 0;
    next_scoreboard_scan_ = {};
    account_display_names_.clear();
    party_identity_scan_ranges_.clear();
    party_identity_scan_range_index_ = 0;
    party_identity_scan_cursor_ = 0;
    party_identity_scan_bytes_ = 0;
    next_party_identity_scan_ = {};
    next_platform_probe_ = {};
    mesh_owner_offset_ = 0;
    runtime::log("UWorld changed; map caches reset world=0x%llX",
                 static_cast<unsigned long long>(world));
}

CameraView GameReader::decode_camera(const CameraCacheRaw& raw) const {
    // Verified size is 0x34. UE5 LWC representation: FVector3d (0x18),
    // FRotator3d (0x18), float FOV (0x4).
    #pragma pack(push, 1)
    struct RemoteCamera {
        double x, y, z;
        double pitch, yaw, roll;
        float fov;
    };
    #pragma pack(pop)
    static_assert(sizeof(RemoteCamera) == 0x34, "camera layout");
    RemoteCamera value{};
    std::memcpy(&value, raw.bytes.data(), sizeof(value));
    CameraView out{
        {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)},
        {static_cast<float>(value.pitch), static_cast<float>(value.yaw), static_cast<float>(value.roll)},
        value.fov,
        false,
    };
    out.valid = plausible_location(out.location) && std::isfinite(out.rotation.pitch) && std::isfinite(out.rotation.yaw)
        && std::isfinite(out.fov) && out.fov >= 10.0F && out.fov <= 170.0F;
    return out;
}

bool GameReader::read_local(LocalSnapshot& out, std::string& error) const {
    const auto encoded = memory_.read<std::uint64_t>(memory_.module_base() + profile_.gworld);
    if (!encoded) { error = "GWorld encrypted read failed"; return false; }
    out.world = decrypt_world(*encoded, profile_);
    if (!memory_.valid_pointer(out.world)) { error = "decrypted UWorld pointer is invalid"; return false; }

    const auto game_instance = memory_.read<std::uintptr_t>(out.world + profile_.world_owning_game_instance);
    if (!game_instance || !memory_.valid_pointer(*game_instance)) { error = "OwningGameInstance unavailable"; return false; }
    out.game_instance = *game_instance;

    const auto players = memory_.read<RemoteArray>(out.game_instance + profile_.game_instance_local_players);
    if (!players || players->count < 1 || players->count > 8 || !memory_.valid_pointer(players->data)) { error = "LocalPlayers array invalid"; return false; }
    const auto local_player = memory_.read<std::uintptr_t>(players->data);
    if (!local_player || !memory_.valid_pointer(*local_player)) { error = "LocalPlayer unavailable"; return false; }
    out.local_player = *local_player;

    const auto controller = memory_.read<std::uintptr_t>(out.local_player + profile_.local_player_controller);
    if (!controller || !memory_.valid_pointer(*controller)) { error = "PlayerController unavailable"; return false; }
    out.controller = *controller;

    if (profile_.controller_acknowledged_pawn != 0) {
        if (const auto pawn = memory_.read<std::uintptr_t>(out.controller + profile_.controller_acknowledged_pawn);
            pawn && memory_.valid_pointer(*pawn)) out.pawn = *pawn;
    }
    if (out.pawn) {
        if (const auto state = memory_.read<std::uintptr_t>(out.pawn + profile_.pawn_player_state); state && memory_.valid_pointer(*state)) out.player_state = *state;
        out.team = read_player_team(memory_, out.player_state, profile_);
        if (const auto root = read_root_component(memory_, out.pawn, profile_.actor_root_component)) {
            if (const auto location = read_scene_location(memory_, *root, profile_.scene_component_location)) out.location = location->narrow();
            if (profile_.scene_component_velocity != 0) {
                if (const auto velocity = memory_.read<Vec3d>(*root + profile_.scene_component_velocity);
                    velocity && velocity->finite()) out.velocity = velocity->narrow();
            }
        }
    }

    if (const auto camera_manager = memory_.read<std::uintptr_t>(out.controller + profile_.controller_camera_manager); camera_manager && memory_.valid_pointer(*camera_manager)) {
        out.camera_manager = *camera_manager;
        CameraCacheRaw raw{};
        if (profile_.camera_cache_size == raw.bytes.size() && memory_.read_bytes(out.camera_manager + profile_.camera_cache, raw.bytes.data(), raw.bytes.size())) out.camera = decode_camera(raw);
    }
    if (profile_.controller_firing_state != 0)
        out.firing = memory_.read<std::uint8_t>(out.controller + profile_.controller_firing_state).value_or(0) != 0;
    if (profile_.controller_aiming_state != 0)
        out.aiming = memory_.read<std::uint8_t>(out.controller + profile_.controller_aiming_state).value_or(0) != 0;
    if (out.pawn && profile_.character_mesh != 0 && profile_.mesh_visibility_time != 0) {
        if (const auto mesh = memory_.read<std::uintptr_t>(out.pawn + profile_.character_mesh);
            mesh && memory_.valid_pointer(*mesh)) {
            if (const auto render_time = memory_.read<float>(*mesh + profile_.mesh_visibility_time);
                render_time && valid_render_time_sample(*render_time) && *render_time > 0.0F) {
                out.mesh_visibility_time = *render_time;
                out.mesh_visibility_known = true;
            }
        }
    }
    out.valid = out.world && out.controller && out.camera.valid;
    if (!out.valid) error = "local chain resolved but camera validation failed";
    return out.valid;
}

bool GameReader::read_bones(EntitySnapshot& entity) const {
    if (!entity.mesh || profile_.bone_stride != sizeof(RemoteTransform96)) return false;
    std::uintptr_t bone_data{};
    std::int32_t bone_count{};
    if (const auto array = read_bone_array(memory_, entity.mesh, profile_.mesh_bone_array)) {
        bone_data = array->data;
        bone_count = array->count;
    }
    if (!bone_data && profile_.mesh_bone_array_fallback != 0) {
        const auto fallback = memory_.read<std::uintptr_t>(entity.mesh + profile_.mesh_bone_array_fallback);
        if (fallback && memory_.valid_pointer(*fallback)) bone_data = *fallback;
    }
    const auto component = memory_.read<RemoteTransform96>(entity.mesh + profile_.mesh_component_to_world);
    if (!bone_data || bone_count <= 0 || !component) return false;
    static constexpr std::array<std::uint16_t, static_cast<std::size_t>(BoneSlot::Count)> ids{110, 67, 37, 9, 66, 2, 73, 80};
    std::array<RemoteTransform96, static_cast<std::size_t>(BoneSlot::Count)> transforms{};
    std::vector<MemoryRead> reads;
    reads.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        // CL-575 returns 109 entries; avoid reading the legacy head index 110
        // past the TArray boundary into adjacent mesh data.
        if (ids[index] >= static_cast<std::uint16_t>(bone_count)) continue;
        reads.push_back({bone_data + static_cast<std::uintptr_t>(ids[index]) * profile_.bone_stride,
                         &transforms[index], sizeof(transforms[index])});
    }
    memory_.read_scatter(reads);
    std::size_t valid_bones{};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto& transform = transforms[index];
        if (!plausible_component_transform(transform)) continue;
        entity.bones[index] = transform_position(transform, *component);
        if (!plausible_location(entity.bones[index]) ||
            (entity.bones[index] - entity.location).length() > 500.0F) {
            entity.bones[index] = {};
            continue;
        }
        ++valid_bones;
    }
    // Head/neck/spine are enough for a stable box and ordinary AIM. Missing
    // foot transforms should not discard an otherwise valid player.
    return valid_bones >= 3;
}

std::uintptr_t GameReader::resolve_mesh_owner_offset(std::uintptr_t local_mesh,
                                                      std::uintptr_t local_pawn) {
    if (!memory_.valid_pointer(local_mesh) || !memory_.valid_pointer(local_pawn)) return 0;
    const auto validates_local_link = [&](std::uintptr_t offset) {
        const auto owner = memory_.read<std::uintptr_t>(local_mesh + offset);
        if (!owner || *owner != local_pawn) return false;
        const auto owner_mesh = memory_.read<std::uintptr_t>(local_pawn + profile_.character_mesh);
        return owner_mesh && *owner_mesh == local_mesh;
    };
    if (mesh_owner_offset_ != 0 && validates_local_link(mesh_owner_offset_)) return mesh_owner_offset_;

    mesh_owner_offset_ = 0;
    std::array<std::uint8_t, kMeshOwnerProbeSize> raw{};
    if (!memory_.read_bytes(local_mesh, raw.data(), raw.size())) return 0;
    for (std::size_t offset = sizeof(std::uintptr_t);
         offset + sizeof(std::uintptr_t) <= raw.size(); offset += sizeof(std::uintptr_t)) {
        std::uintptr_t value{};
        std::memcpy(&value, raw.data() + offset, sizeof(value));
        if (value != local_pawn || !validates_local_link(offset)) continue;
        mesh_owner_offset_ = offset;
        return offset;
    }
    return 0;
}

bool GameReader::update_render_freshness(std::uintptr_t actor, std::uintptr_t mesh,
                                         const std::optional<float>& render_time,
                                         std::chrono::steady_clock::time_point now) {
    auto& temporal = actor_temporal_[actor];
    if (temporal.mesh != mesh) {
        temporal = {};
        temporal.mesh = mesh;
    }
    if (!render_time || !valid_render_time_sample(*render_time)) return false;

    if (!temporal.render_initialized) {
        temporal.last_render_time = *render_time;
        temporal.render_initialized = true;
        temporal.last_render_advance = now;
        temporal.observed_render_advance = true;
        return true;
    }
    if (*render_time > temporal.last_render_time + kRenderAdvanceEpsilon) {
        temporal.last_render_time = *render_time;
        temporal.last_render_advance = now;
        temporal.observed_render_advance = true;
    } else if (*render_time < temporal.last_render_time - kRenderAdvanceEpsilon) {
        temporal.last_render_time = *render_time;
        temporal.last_render_advance = {};
        temporal.observed_render_advance = false;
    }
    return temporal.observed_render_advance && now - temporal.last_render_advance <= kRenderFreshness;
}

void GameReader::apply_render_visibility(EntitySnapshot& entity, const LocalSnapshot& local,
                                         const std::optional<float>& render_time,
                                         std::chrono::steady_clock::time_point now) {
    entity.visibility_known = render_time && valid_render_time_sample(*render_time) && *render_time > 0.0F;
    if (!entity.visibility_known) {
        entity.visible = true;
        return;
    }
    if (local.mesh_visibility_known) {
        entity.visible = *render_time + kRenderTimeSlack >= local.mesh_visibility_time;
        update_render_freshness(entity.actor, entity.mesh, render_time, now);
        return;
    }
    entity.visible = update_render_freshness(entity.actor, entity.mesh, render_time, now);
}

bool GameReader::update_mesh_motion_freshness(std::uintptr_t actor, std::uintptr_t mesh,
                                              const Vec3& mesh_center,
                                              std::chrono::steady_clock::time_point now) {
    auto& temporal = actor_temporal_[actor];
    if (temporal.mesh != mesh) {
        temporal = {};
        temporal.mesh = mesh;
    }
    if (!plausible_location(mesh_center)) return false;
    if (!temporal.mesh_location_initialized) {
        temporal.last_mesh_center = mesh_center;
        temporal.mesh_location_initialized = true;
        return false;
    }
    const Vec3 delta = mesh_center - temporal.last_mesh_center;
    const float movement = delta.length();
    if (movement > kMeshMotionTeleport) {
        temporal.last_mesh_center = mesh_center;
        temporal.last_mesh_motion = {};
        temporal.observed_mesh_motion = false;
    } else if (movement > kMeshMotionEpsilon) {
        temporal.last_mesh_center = mesh_center;
        temporal.last_mesh_motion = now;
        temporal.observed_mesh_motion = true;
    }
    return temporal.observed_mesh_motion && now - temporal.last_mesh_motion <= kMeshMotionFreshness;
}

std::optional<EntitySnapshot> GameReader::probe_mesh(std::uintptr_t mesh, const LocalSnapshot& local,
                                                     MeshProbeDiagnostics& diagnostics) const {
    diagnostics = {};
    diagnostics.address = mesh;
    if (!memory_.valid_pointer(mesh)) return std::nullopt;

    const auto bone_array = memory_.read<RemoteArray>(mesh + profile_.mesh_bone_array);
    diagnostics.bone_array = bone_array && memory_.valid_pointer(bone_array->data) &&
                              bone_array->count >= 111 && bone_array->count <= 512 &&
                              bone_array->capacity >= bone_array->count && bone_array->capacity <= 512;

    const auto component = memory_.read<RemoteTransform96>(mesh + profile_.mesh_component_to_world);
    diagnostics.component_transform = component && plausible_component_transform(*component);
    if (component) {
        diagnostics.location = {static_cast<float>(component->tx), static_cast<float>(component->ty),
                                static_cast<float>(component->tz)};
    }

    const auto render_time = memory_.read<float>(mesh + profile_.mesh_visibility_time);
    if (render_time) diagnostics.render_time_value = *render_time;
    diagnostics.render_time = render_time && valid_render_time_sample(*render_time);

    if (!diagnostics.bone_array || !diagnostics.component_transform)
        return std::nullopt;

    EntitySnapshot entity{};
    entity.mesh = mesh;
    entity.mesh_signature_fallback = true;
    entity.location = diagnostics.location;
    entity.distance_m = (entity.location - local.camera.location).length() / 100.0F;
    entity.visibility_known = render_time && valid_render_time_sample(*render_time) && *render_time > 0.0F;
    entity.visible = entity.visibility_known ? currently_rendered(*render_time) : true;
    diagnostics.distance_m = entity.distance_m;
    diagnostics.bones = read_bones(entity);
    diagnostics.accepted = diagnostics.bones && plausible_location(entity.location) &&
                           std::isfinite(entity.distance_m) && entity.distance_m <= 5000.0F;
    if (!diagnostics.accepted) return std::nullopt;
    return entity;
}

void GameReader::discover_mesh_entities(const LocalSnapshot& local, std::vector<EntitySnapshot>& out,
                                        EntityDiagnostics& diagnostics) {
    if (!local.pawn) return;
    diagnostics.stage = "mesh_signature_scan";
    const std::uintptr_t local_mesh = memory_.read<std::uintptr_t>(
        local.pawn + profile_.character_mesh).value_or(0);
    if (!memory_.valid_pointer(local_mesh)) return;
    const bool pawn_changed = mesh_scan_owner_ != local.pawn;
    if (pawn_changed || mesh_scan_ranges_.empty()) {
        // Remote skeletal meshes can be allocated in several anonymous UE
        // heaps. The Owner <-> Pawn.Mesh link below is the trust boundary, so
        // retain all plausible heap mappings instead of just the local one.
        const auto local_mapping = memory_.mapping_for(local_mesh);
        const auto add_range = [&](const MemoryRange& range) {
            if (std::none_of(mesh_scan_ranges_.begin(), mesh_scan_ranges_.end(),
                             [&](const MemoryRange& existing) {
                                 return existing.start == range.start && existing.end == range.end;
                             }))
                mesh_scan_ranges_.push_back(range);
        };
        mesh_scan_ranges_.clear();
        if (local_mapping && local_mapping->writable && local_mapping->private_mapping &&
            !local_mapping->executable && local_mapping->end > local_mapping->start &&
            local_mapping->end - local_mapping->start >= kMeshScanMinMapping)
            add_range(*local_mapping);
        for (const auto& range : memory_.readable_mappings()) {
            const bool anonymous = range.path.empty() || range.path.front() == '[';
            if (!range.writable || !range.private_mapping || range.executable || !anonymous ||
                range.end <= range.start || range.end - range.start < kMeshScanMinMapping)
                continue;
            add_range(range);
        }
        if (mesh_scan_ranges_.empty()) return;
        mesh_scan_owner_ = local.pawn;
        mesh_scan_range_index_ = 0;
        mesh_scan_cursor_ = mesh_scan_ranges_.front().start;
        if (local_mapping && mesh_scan_ranges_.front().start == local_mapping->start &&
            mesh_scan_ranges_.front().end == local_mapping->end && local_mesh >= local_mapping->start) {
            const std::uintptr_t available = local_mesh - local_mapping->start;
            mesh_scan_cursor_ = local_mesh - std::min(available, kMeshScanLocalLead);
            mesh_scan_cursor_ -= mesh_scan_cursor_ % kMeshScanReadBlock;
        }
        mesh_scan_end_ = std::min(mesh_scan_ranges_.front().end,
                                  mesh_scan_cursor_ + kMeshScanWindow);
        mesh_signature_tests_ = 0;
        mesh_signature_rejected_ = 0;
        mesh_scan_blocks_ok_ = 0;
        mesh_scan_blocks_failed_ = 0;
        if (pawn_changed) {
            discovered_meshes_.clear();
            mesh_cache_misses_.clear();
            mesh_owner_offset_ = 0;
        }
    }

    if (mesh_scan_cursor_ >= mesh_scan_end_ && !mesh_scan_ranges_.empty()) {
        mesh_scan_range_index_ = (mesh_scan_range_index_ + 1) % mesh_scan_ranges_.size();
        const auto& range = mesh_scan_ranges_[mesh_scan_range_index_];
        mesh_scan_cursor_ = range.start;
        mesh_scan_end_ = std::min(range.end, range.start + kMeshScanWindow);
    }
    const std::uintptr_t owner_offset = resolve_mesh_owner_offset(local_mesh, local.pawn);
    diagnostics.mesh_owner_offset = owner_offset;
    diagnostics.mesh_scan_ranges = static_cast<std::int32_t>(std::min<std::size_t>(
        mesh_scan_ranges_.size(), std::numeric_limits<std::int32_t>::max()));
    diagnostics.mesh_scan_range_index = mesh_scan_ranges_.empty() ? 0 : static_cast<std::int32_t>(
        std::min<std::size_t>(mesh_scan_range_index_ + 1, std::numeric_limits<std::int32_t>::max()));

    if (mesh_scan_cursor_ < mesh_scan_end_) {
        const std::size_t chunk = take_scan_budget(discovered_meshes_.empty() ? kMeshScanRecoveryChunk : kMeshScanChunk);
        const std::size_t primary = static_cast<std::size_t>(std::min<std::uintptr_t>(chunk, mesh_scan_end_ - mesh_scan_cursor_));
        constexpr std::size_t overlap = 0x680;
        std::vector<std::uint8_t> buffer(kMeshScanReadBlock + overlap);
        for (std::size_t block_offset = 0; block_offset < primary; block_offset += kMeshScanReadBlock) {
            const std::uintptr_t block_address = mesh_scan_cursor_ + block_offset;
            const std::size_t block_primary = std::min(kMeshScanReadBlock, primary - block_offset);
            const std::size_t requested = static_cast<std::size_t>(std::min<std::uintptr_t>(
                block_primary + overlap, mesh_scan_end_ - block_address));
            if (!memory_.read_bytes(block_address, buffer.data(), requested)) {
                ++mesh_scan_blocks_failed_;
                continue;
            }
            ++mesh_scan_blocks_ok_;
            for (std::size_t offset = 0; offset + 8 <= block_primary; offset += 4) {
                std::int32_t count{}, capacity{};
                std::memcpy(&count, buffer.data() + offset, sizeof(count));
                std::memcpy(&capacity, buffer.data() + offset + 4, sizeof(capacity));
                if (count < 111 || count > 512 || capacity < count || capacity > 512) continue;
                ++mesh_signature_tests_;
                const std::uintptr_t count_address = block_address + offset;
                if (count_address < profile_.mesh_bone_array + 8) { ++mesh_signature_rejected_; continue; }
                const std::uintptr_t mesh = count_address - (profile_.mesh_bone_array + 8);
                if (mesh == local_mesh || !memory_.valid_pointer(mesh)) { ++mesh_signature_rejected_; continue; }
                if (owner_offset == 0) { ++mesh_signature_rejected_; continue; }
                const auto owner = memory_.read<std::uintptr_t>(mesh + owner_offset);
                if (!owner || !memory_.valid_pointer(*owner) || *owner == local.pawn) {
                    ++mesh_signature_rejected_;
                    continue;
                }
                const auto owner_mesh = memory_.read<std::uintptr_t>(*owner + profile_.character_mesh);
                if (!owner_mesh || *owner_mesh != mesh) {
                    ++mesh_signature_rejected_;
                    continue;
                }
                const auto bones = memory_.read<RemoteArray>(mesh + profile_.mesh_bone_array);
                const auto component = memory_.read<RemoteTransform96>(mesh + profile_.mesh_component_to_world);
                if (!bones || !component || !memory_.valid_pointer(bones->data) ||
                    bones->count != count || bones->capacity != capacity ||
                    !plausible_component_transform(*component)) {
                    ++mesh_signature_rejected_;
                    continue;
                }
                EntitySnapshot candidate{};
                candidate.mesh = mesh;
                candidate.location = {static_cast<float>(component->tx), static_cast<float>(component->ty),
                                      static_cast<float>(component->tz)};
                candidate.distance_m = (candidate.location - local.camera.location).length() / 100.0F;
                if (!plausible_location(candidate.location) || !std::isfinite(candidate.distance_m) ||
                    candidate.distance_m > 5000.0F || !read_bones(candidate)) {
                    ++mesh_signature_rejected_;
                    continue;
                }
                if (std::find(discovered_meshes_.begin(), discovered_meshes_.end(), mesh) == discovered_meshes_.end()) {
                    if (discovered_meshes_.size() >= kMaxDiscoveredMeshes) { ++mesh_signature_rejected_; continue; }
                    discovered_meshes_.push_back(mesh);
                }
                mesh_cache_misses_[mesh] = 0;
            }
        }
        mesh_scan_cursor_ += primary;
    }

    std::vector<std::uintptr_t> retained_meshes;
    retained_meshes.reserve(discovered_meshes_.size());
    for (const std::uintptr_t mesh : discovered_meshes_) {
        if (mesh == local_mesh) continue;
        const auto visibility = memory_.read<float>(mesh + profile_.mesh_visibility_time);
        const auto component = memory_.read<RemoteTransform96>(mesh + profile_.mesh_component_to_world);
        auto retain_temporarily = [&]() {
            auto& misses = mesh_cache_misses_[mesh];
            if (misses < std::numeric_limits<std::uint16_t>::max()) ++misses;
            if (misses <= kMeshCacheMissGrace) retained_meshes.push_back(mesh);
            else mesh_cache_misses_.erase(mesh);
        };
        if (!component || !plausible_component_transform(*component)) {
            retain_temporarily();
            continue;
        }
        // A candidate is a remote Pawn mesh only when the Owner link inferred
        // from the local Pawn is valid in both directions. This removes local
        // attachments, weapon meshes and unrelated skeletal scene objects.
        if (owner_offset == 0) {
            retain_temporarily();
            continue;
        }
        const auto owner = memory_.read<std::uintptr_t>(mesh + owner_offset);
        if (!owner || !memory_.valid_pointer(*owner)) {
            ++diagnostics.mesh_owner_rejected;
            mesh_cache_misses_.erase(mesh);
            continue;
        }
        if (*owner == local.pawn) {
            mesh_cache_misses_.erase(mesh);
            continue;
        }
        const auto owner_mesh = memory_.read<std::uintptr_t>(*owner + profile_.character_mesh);
        if (!owner_mesh || *owner_mesh != mesh) {
            ++diagnostics.mesh_owner_rejected;
            mesh_cache_misses_.erase(mesh);
            continue;
        }
        const auto player_state = profile_.actor_player_state != 0
            ? memory_.read<std::uintptr_t>(*owner + profile_.actor_player_state)
            : std::nullopt;
        const auto current_pawn = (profile_.player_state_pawn_private != 0 &&
                                   player_state && memory_.valid_pointer(*player_state))
            ? memory_.read<std::uintptr_t>(
                  *player_state + profile_.player_state_pawn_private)
            : std::nullopt;
        if (current_pawn) {
            ++diagnostics.pawn_private_read;
            if (*current_pawn == *owner) {
                ++diagnostics.pawn_private_matches;
            } else {
                ++diagnostics.stale_pawn_suppressed;
                actor_temporal_.erase(*owner);
                mesh_cache_misses_.erase(mesh);
                continue;
            }
        }
        const bool already_emitted = std::any_of(out.begin(), out.end(),
            [mesh](const EntitySnapshot& existing) { return existing.mesh == mesh; });
        if (already_emitted) {
            mesh_cache_misses_[mesh] = 0;
            retained_meshes.push_back(mesh);
            continue;
        }
        EntitySnapshot entity{};
        entity.actor = *owner;
        entity.player_state = player_state && memory_.valid_pointer(*player_state)
            ? *player_state : 0;
        entity.team = read_player_team(memory_, entity.player_state, profile_);
        populate_player_identity(entity);
        entity.mesh = mesh;
        entity.mesh_signature_fallback = true;
        entity.location = mesh_capsule_center(*component);
        const auto now = std::chrono::steady_clock::now();
        // Keep validated entities in the output regardless of visibility so
        // occluded players remain red. Compare LastRenderTime with the local
        // pawn clock; the freshness window is the fallback.
        apply_render_visibility(entity, local, visibility, now);
        const auto root = read_root_component(memory_, entity.actor,
                                              profile_.actor_root_component);
        const bool root_valid = root && memory_.valid_pointer(*root);
        const auto live_root_location = root_valid
            ? read_scene_location(memory_, *root, profile_.scene_component_location)
            : std::nullopt;
        if (root_valid) entity.root_component = *root;
        const Vec3 motion_source = live_root_location && live_root_location->finite()
            ? live_root_location->narrow() : entity.location;
        entity.mesh_motion_fresh = update_mesh_motion_freshness(
            entity.actor, mesh, motion_source, now);
        if (!read_bones(entity)) {
            retain_temporarily();
            continue;
        }
        align_to_live_root(entity, *component, live_root_location);
        entity.distance_m = (entity.location - local.camera.location).length() / 100.0F;
        if (!plausible_location(entity.location) || !std::isfinite(entity.distance_m) ||
            entity.distance_m > 5000.0F) {
            retain_temporarily();
            continue;
        }
        mesh_cache_misses_[mesh] = 0;
        retained_meshes.push_back(mesh);
        ++diagnostics.mesh_owner_matches;
        ++diagnostics.bones_ok;
        ++diagnostics.accepted_fallback;
        ++diagnostics.accepted_players;
        out.push_back(entity);
    }
    discovered_meshes_.swap(retained_meshes);
    diagnostics.mesh_signature_tests = static_cast<std::int32_t>(std::min<std::uint64_t>(mesh_signature_tests_, std::numeric_limits<std::int32_t>::max()));
    diagnostics.mesh_signature_rejected = static_cast<std::int32_t>(std::min<std::uint64_t>(mesh_signature_rejected_, std::numeric_limits<std::int32_t>::max()));
    diagnostics.mesh_signature_cached = static_cast<std::int32_t>(discovered_meshes_.size());
    diagnostics.mesh_scan_blocks_ok = static_cast<std::int32_t>(std::min<std::uint64_t>(mesh_scan_blocks_ok_, std::numeric_limits<std::int32_t>::max()));
    diagnostics.mesh_scan_blocks_failed = static_cast<std::int32_t>(std::min<std::uint64_t>(mesh_scan_blocks_failed_, std::numeric_limits<std::int32_t>::max()));
    if (diagnostics.accepted_fallback > 0) diagnostics.stage = "mesh_signature_fallback";
}

void GameReader::scan_player_actor_ranges(const LocalSnapshot& local,
                                          const std::vector<MemoryRange>& ranges,
                                          std::size_t& range_index, std::uintptr_t& cursor,
                                          std::size_t budget) {
    std::vector<std::uint8_t> buffer(kActorScanReadBlock);
    while (budget && range_index < ranges.size()) {
        const auto& range = ranges[range_index];
        if (cursor < range.start) cursor = range.start;
        if (cursor >= range.end) {
            ++range_index;
            if (range_index < ranges.size()) cursor = ranges[range_index].start;
            continue;
        }
        const std::size_t primary = static_cast<std::size_t>(
            std::min<std::uintptr_t>(budget, range.end - cursor));
        for (std::size_t offset = 0; offset < primary; offset += kActorScanReadBlock) {
            const auto block_address = cursor + offset;
            const auto block_size = std::min(kActorScanReadBlock, primary - offset);
            if (!memory_.read_bytes(block_address, buffer.data(), block_size)) continue;
            for (std::size_t i = 0; i + sizeof(std::uint64_t) <= block_size; i += alignof(void*)) {
                std::uint64_t fingerprint{};
                std::memcpy(&fingerprint, buffer.data() + i, sizeof(fingerprint));
                if (!is_player_actor_fingerprint(fingerprint)) continue;
                const auto fingerprint_address = block_address + i;
                if (fingerprint_address < profile_.actor_class_fingerprint) continue;
                const auto actor = fingerprint_address - profile_.actor_class_fingerprint;
                if (actor == local.pawn || !memory_.valid_pointer(actor)) continue;
                const auto mesh = memory_.read<std::uintptr_t>(actor + profile_.character_mesh);
                const auto root = read_root_component(memory_, actor, profile_.actor_root_component);
                if (!mesh || !root || !memory_.valid_pointer(*mesh) || !memory_.valid_pointer(*root)) continue;
                const auto player_state = profile_.actor_player_state != 0
                    ? memory_.read<std::uintptr_t>(actor + profile_.actor_player_state)
                    : std::nullopt;
                const auto current_pawn = (profile_.player_state_pawn_private != 0 &&
                                           player_state && memory_.valid_pointer(*player_state))
                    ? memory_.read<std::uintptr_t>(
                          *player_state + profile_.player_state_pawn_private)
                    : std::nullopt;
                if (current_pawn && *current_pawn != actor) {
                    continue;
                }
                const auto component = memory_.read<RemoteTransform96>(*mesh + profile_.mesh_component_to_world);
                const auto root_location = read_scene_location(memory_, *root, profile_.scene_component_location);
                if (!component || !plausible_component_transform(*component) ||
                    !root_location || !root_location->finite()) continue;
                if (std::find(discovered_player_actors_.begin(), discovered_player_actors_.end(), actor) ==
                    discovered_player_actors_.end())
                    discovered_player_actors_.push_back(actor);
                if (std::none_of(actor_hot_ranges_.begin(), actor_hot_ranges_.end(),
                                 [&](const MemoryRange& hot) {
                                     return hot.start == range.start && hot.end == range.end;
                                 }))
                    actor_hot_ranges_.push_back(range);
                actor_cache_misses_[actor] = 0;
            }
        }
        cursor += primary;
        budget -= primary;
    }
}

void GameReader::discover_player_actors(const LocalSnapshot& local, std::vector<EntitySnapshot>& out,
                                        EntityDiagnostics& diagnostics) {
    if (!local.pawn) return;
    diagnostics.stage = "player_fingerprint_scan";
    const pid_t current_pid = memory_.pid();
    if (actor_scan_pid_ != current_pid || actor_scan_ranges_.empty()) {
        actor_scan_pid_ = current_pid;
        actor_scan_owner_ = local.pawn;
        actor_scan_ranges_.clear();
        actor_hot_ranges_.clear();
        for (const auto& range : memory_.readable_mappings()) {
            if (!range.writable || range.end <= range.start || range.end - range.start < kActorScanMinMapping) continue;
            actor_scan_ranges_.push_back(range);
        }
        actor_scan_range_index_ = 0;
        actor_scan_cursor_ = actor_scan_ranges_.empty() ? 0 : actor_scan_ranges_.front().start;
        actor_cold_range_index_ = 0;
        actor_cold_cursor_ = actor_scan_cursor_;
        if (const auto local_mapping = memory_.mapping_for(local.pawn);
            local_mapping && local_mapping->writable &&
            local_mapping->end > local_mapping->start &&
            local_mapping->end - local_mapping->start >= kActorScanMinMapping)
            actor_hot_ranges_.push_back(*local_mapping);
        actor_scan_complete_ = false;
        discovered_player_actors_.clear();
        actor_cache_misses_.clear();
        actor_temporal_.clear();
        actor_mesh_cache_.clear();
        actor_mesh_probe_misses_.clear();
    } else if (actor_scan_owner_ != local.pawn) {
        // A respawn swaps the local Pawn but keeps remote player allocations in
        // the same process. Preserve discovered actors and hot mappings, then
        // immediately rescan the hot set while the cold pass continues every frame.
        actor_scan_owner_ = local.pawn;
        if (const auto local_mapping = memory_.mapping_for(local.pawn);
            local_mapping && local_mapping->writable &&
            local_mapping->end > local_mapping->start &&
            local_mapping->end - local_mapping->start >= kActorScanMinMapping &&
            std::none_of(actor_hot_ranges_.begin(), actor_hot_ranges_.end(),
                         [&](const MemoryRange& hot) {
                             return hot.start == local_mapping->start && hot.end == local_mapping->end;
                         }))
            actor_hot_ranges_.push_back(*local_mapping);
        actor_scan_complete_ = true;
        actor_scan_range_index_ = 0;
        actor_scan_cursor_ = !actor_hot_ranges_.empty()
            ? actor_hot_ranges_.front().start : actor_scan_ranges_.front().start;
    }

    const auto& active_scan_ranges = actor_scan_complete_ && !actor_hot_ranges_.empty()
        ? actor_hot_ranges_ : actor_scan_ranges_;
    diagnostics.actor_hot_ranges = static_cast<std::int32_t>(actor_hot_ranges_.size());
    const bool hot_scan_due = !actor_scan_complete_ || sequence_ % kActorHotScanStride == 0U;
    const std::size_t budget = actor_scan_complete_ ? kActorScanRescanChunk : kActorScanChunk;
    if (hot_scan_due)
        scan_player_actor_ranges(local, active_scan_ranges, actor_scan_range_index_, actor_scan_cursor_, take_scan_budget(std::min(budget,scan_budget_bytes_.load(std::memory_order_relaxed)/2)));
    if (actor_scan_range_index_ >= active_scan_ranges.size() && !active_scan_ranges.empty()) {
        actor_scan_complete_ = true;
        actor_scan_range_index_ = 0;
        actor_scan_cursor_ = !actor_hot_ranges_.empty()
            ? actor_hot_ranges_.front().start : actor_scan_ranges_.front().start;
    }
    if (actor_scan_complete_ && !actor_scan_ranges_.empty() &&
        sequence_ % kActorColdScanStride == 0U) {
        scan_player_actor_ranges(local, actor_scan_ranges_, actor_cold_range_index_,
                                 actor_cold_cursor_, take_scan_budget(std::min(kActorColdScanChunk,scan_remaining_/2)));
        if (actor_cold_range_index_ >= actor_scan_ranges_.size()) {
            actor_cold_range_index_ = 0;
            actor_cold_cursor_ = actor_scan_ranges_.front().start;
        }
    }
    diagnostics.actor_hot_ranges = static_cast<std::int32_t>(actor_hot_ranges_.size());

    std::vector<std::uintptr_t> retained;
    retained.reserve(discovered_player_actors_.size());
    auto already_emitted = [&](std::uintptr_t actor, std::uintptr_t mesh) {
        return std::any_of(out.begin(), out.end(), [&](const EntitySnapshot& entity) {
            return (actor && entity.actor == actor) || (mesh && entity.mesh == mesh);
        });
    };
    for (const auto actor : discovered_player_actors_) {
        const auto player_state = profile_.actor_player_state != 0
            ? memory_.read<std::uintptr_t>(actor + profile_.actor_player_state)
            : std::nullopt;
        const auto current_pawn = (profile_.player_state_pawn_private != 0 &&
                                   player_state && memory_.valid_pointer(*player_state))
            ? memory_.read<std::uintptr_t>(
                  *player_state + profile_.player_state_pawn_private)
            : std::nullopt;
        if (current_pawn) {
            ++diagnostics.pawn_private_read;
            if (*current_pawn == actor) {
                ++diagnostics.pawn_private_matches;
            } else {
                ++diagnostics.stale_pawn_suppressed;
                actor_temporal_.erase(actor);
                actor_cache_misses_.erase(actor);
                continue;
            }
        }

        // World-array entities still pass through the lifecycle check above;
        // only a failed backlink read gets the short identity grace path.
        if (std::any_of(out.begin(), out.end(), [&](const EntitySnapshot& entity) {
                return entity.actor == actor && entity.mesh;
            })) {
            actor_cache_misses_[actor] = 0;
            retained.push_back(actor);
            continue;
        }
        const auto root = read_root_component(memory_, actor, profile_.actor_root_component);
        const auto mesh = memory_.read<std::uintptr_t>(actor + profile_.character_mesh);
        const auto root_location = root
            ? read_scene_location(memory_, *root, profile_.scene_component_location) : std::nullopt;
        const auto component = (mesh && memory_.valid_pointer(*mesh))
            ? memory_.read<RemoteTransform96>(*mesh + profile_.mesh_component_to_world) : std::nullopt;
        const bool root_location_valid = profile_.actor_root_component != 0 &&
            profile_.scene_component_location != 0 && root && memory_.valid_pointer(*root) &&
            root_location && root_location->finite() && plausible_location(root_location->narrow());
        if (!mesh || !memory_.valid_pointer(*mesh) || !root_location_valid) {
            auto& misses = actor_cache_misses_[actor];
            if (misses < std::numeric_limits<std::uint16_t>::max()) ++misses;
            if (misses <= kMeshCacheMissGrace) retained.push_back(actor);
            else {
                actor_cache_misses_.erase(actor);
                actor_temporal_.erase(actor);
            }
            continue;
        }
        actor_cache_misses_[actor] = 0;
        EntitySnapshot entity{};
        entity.actor = actor;
        entity.root_component = profile_.actor_root_component != 0 && root &&
            memory_.valid_pointer(*root) ? *root : 0;
        entity.mesh = *mesh;
        entity.location = root_location->narrow();
        entity.distance_m = (entity.location - local.camera.location).length() / 100.0F;
        entity.player_state = player_state && memory_.valid_pointer(*player_state) ? *player_state : 0;
        entity.team = read_player_team(memory_, entity.player_state, profile_);
        populate_player_identity(entity);
        const auto render_time = memory_.read<float>(*mesh + profile_.mesh_visibility_time);
        const auto now = std::chrono::steady_clock::now();
        apply_render_visibility(entity, local, render_time, now);
        const bool bones_read = read_bones(entity);
        if (component && plausible_component_transform(*component)) {
            const Vec3 motion_source = root_location->narrow();
            entity.mesh_motion_fresh = update_mesh_motion_freshness(actor, *mesh, motion_source, now);
            align_to_live_root(entity, *component, root_location);
        }
        entity.distance_m = (entity.location - local.camera.location).length() / 100.0F;
        if (plausible_location(entity.location) && std::isfinite(entity.distance_m) && entity.distance_m <= 5000.0F &&
            !already_emitted(entity.actor, entity.mesh)) {
            out.push_back(entity);
            ++diagnostics.accepted_players;
            ++diagnostics.current_pawn_ok;
            ++diagnostics.geometry_ok;
            ++diagnostics.root_ok;
            ++diagnostics.mesh_ok;
            if (entity.visual_mesh_aligned) ++diagnostics.mesh_aligned_boxes;
            if (entity.mesh_motion_fresh) ++diagnostics.mesh_motion_fresh;
            if (entity.visible) ++diagnostics.render_fresh;
            if (bones_read) ++diagnostics.bones_ok;
            else ++diagnostics.root_position_only;
        }
        retained.push_back(actor);
    }
    discovered_player_actors_.swap(retained);
    if (diagnostics.accepted_players > 0) diagnostics.stage = "player_actor_global";
}

void GameReader::read_entities(const LocalSnapshot& local, std::vector<EntitySnapshot>& out, EntityDiagnostics& diagnostics) {
    diagnostics.stage = "reading_world_levels";
    const auto levels = memory_.read<RemoteArray>(local.world + profile_.world_actor_array);
    if (!levels) { diagnostics.stage = "world_levels_read_failed"; return; }
    if (levels->count < 1 || levels->count > kDonorLevelCountLimit ||
        !memory_.valid_pointer(levels->data)) {
        diagnostics.stage = "world_levels_invalid";
        return;
    }
    std::vector<std::uintptr_t> level_pointers(static_cast<std::size_t>(levels->count));
    if (!memory_.read_bytes(levels->data, level_pointers.data(),
                            level_pointers.size() * sizeof(level_pointers.front()))) {
        diagnostics.stage = "world_levels_bulk_read_failed";
        return;
    }

    std::vector<std::uintptr_t> actor_pointers;
    actor_pointers.reserve(static_cast<std::size_t>(kDonorActorCountLimit));
    std::int64_t reported_actor_count{};
    for (const auto level : level_pointers) {
        if (!memory_.valid_pointer(level) || profile_.level_actor_array == 0) continue;
        const auto actors = memory_.read<RemoteArray>(level + profile_.level_actor_array);
        if (!actors || actors->count < 1 || actors->count > kDonorActorCountLimit ||
            !memory_.valid_pointer(actors->data))
            continue;
        reported_actor_count += actors->count;
        const auto remaining = static_cast<std::size_t>(kDonorActorCountLimit) -
                               actor_pointers.size();
        const auto take = std::min<std::size_t>(
            static_cast<std::size_t>(actors->count), remaining);
        if (take == 0) break;
        const auto begin = actor_pointers.size();
        actor_pointers.resize(begin + take);
        if (!memory_.read_bytes(actors->data, actor_pointers.data() + begin,
                                take * sizeof(actor_pointers.front()))) {
            actor_pointers.resize(begin);
        }
    }
    diagnostics.actor_count = static_cast<std::int32_t>(std::min<std::int64_t>(
        reported_actor_count, std::numeric_limits<std::int32_t>::max()));
    // Streaming levels can expose the same actor through more than one Actor
    // TArray during a transition. Preserve donor order while ensuring one
    // snapshot, one box, and one AIM candidate per actor.
    std::unordered_set<std::uintptr_t> unique_actors;
    unique_actors.reserve(actor_pointers.size());
    actor_pointers.erase(
        std::remove_if(actor_pointers.begin(), actor_pointers.end(),
                       [&](std::uintptr_t actor) {
                           return !memory_.valid_pointer(actor) ||
                                  !unique_actors.insert(actor).second;
                       }),
        actor_pointers.end());
    diagnostics.actors_scanned = static_cast<std::int32_t>(actor_pointers.size());
    if (actor_pointers.empty()) { diagnostics.stage = "level_actor_arrays_empty"; return; }
    std::vector<std::uint64_t> fingerprints(actor_pointers.size());
    std::vector<MemoryRead> fingerprint_reads;
    fingerprint_reads.reserve(actor_pointers.size());
    for (std::size_t index = 0; index < actor_pointers.size(); ++index) {
        const auto actor = actor_pointers[index];
        if (actor == local.pawn) continue;
        fingerprint_reads.push_back({actor + profile_.actor_class_fingerprint,
                                     &fingerprints[index], sizeof(fingerprints[index])});
    }
    memory_.read_scatter(fingerprint_reads);

    // HX-42 batches Actor+0x58 first and only performs expensive object-chain
    // reads for its two recognized classes. This reduces a 50,000-Actor world
    // from tens of thousands of syscalls to roughly 100 scatter calls plus the
    // small live player set.
    std::vector<std::pair<std::uintptr_t, std::uint64_t>> candidates;
    candidates.reserve(128);
    for (std::size_t index = 0; index < actor_pointers.size(); ++index) {
        const auto actor = actor_pointers[index];
        const auto fingerprint = fingerprints[index];
        if (actor == local.pawn) continue;
        ++diagnostics.fingerprint_read;
        if (is_player_actor_fingerprint(fingerprint)) {
            ++diagnostics.player_fingerprint;
            candidates.emplace_back(actor, fingerprint);
        } else if (include_world_actors_.load(std::memory_order_relaxed) && fingerprint == kWorldActorFingerprint) {
            ++diagnostics.world_fingerprint;
            candidates.emplace_back(actor, fingerprint);
        } else {
            ++diagnostics.fingerprint_unknown;
        }
    }
    out.reserve(candidates.size());
    const std::uintptr_t local_mesh = local.pawn
        ? memory_.read<std::uintptr_t>(local.pawn + profile_.character_mesh).value_or(0) : 0;
    std::size_t actor_mesh_probe_budget = kActorMeshProbeBudgetPerFrame;

    struct PlayerPrefetch {
        std::uintptr_t player_state{};
        std::uintptr_t root{};
        std::uintptr_t mesh{};
    };
    std::vector<PlayerPrefetch> prefetch(candidates.size());
    std::vector<MemoryRead> pointer_reads;
    pointer_reads.reserve(candidates.size() * 3U);
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!is_player_actor_fingerprint(candidates[index].second)) continue;
        const auto actor = candidates[index].first;
        pointer_reads.push_back({actor + profile_.actor_player_state,
                                 &prefetch[index].player_state, sizeof(std::uintptr_t)});
        pointer_reads.push_back({actor + profile_.actor_root_component,
                                 &prefetch[index].root, sizeof(std::uintptr_t)});
        pointer_reads.push_back({actor + profile_.character_mesh,
                                 &prefetch[index].mesh, sizeof(std::uintptr_t)});
    }
    memory_.read_scatter(pointer_reads);

    // Equivalent indexed form of: for (const auto& [actor, fingerprint] : candidates)
    for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        const auto [actor, fingerprint] = candidates[candidate_index];
        ++diagnostics.pointer_ok;
        std::uintptr_t resolved_player_state{};
        int resolved_team{-1};
        bool current_pawn_matches = false;
        bool current_pawn_mismatch = false;
        const auto prefetched = prefetch[candidate_index];
        const auto player_state = is_player_actor_fingerprint(fingerprint) &&
            profile_.actor_player_state != 0 && memory_.valid_pointer(prefetched.player_state)
            ? std::optional<std::uintptr_t>(prefetched.player_state) : std::nullopt;
        if (player_state && memory_.valid_pointer(*player_state) && *player_state != local.player_state) {
            ++diagnostics.player_state_candidates;
            ++diagnostics.player_state_ok;
            resolved_player_state = *player_state;
            resolved_team = read_player_team(memory_, *player_state, profile_);
            if (local.team >= 0 && resolved_team == local.team) ++diagnostics.team_filtered;
            const auto current_pawn = profile_.player_state_pawn_private != 0
                ? memory_.read<std::uintptr_t>(
                      *player_state + profile_.player_state_pawn_private)
                : std::nullopt;
            if (current_pawn) {
                ++diagnostics.pawn_private_read;
                current_pawn_matches = *current_pawn == actor;
                current_pawn_mismatch = !current_pawn_matches &&
                    memory_.valid_pointer(*current_pawn);
                if (current_pawn_matches) ++diagnostics.pawn_private_matches;
            }
            if (current_pawn_mismatch) {
                ++diagnostics.stale_pawn_suppressed;
                actor_temporal_.erase(actor);
                continue;
            }
        }

        // Independent rendering fallback: PlayerState and team semantics can
        // vary in Creative maps. A character-like actor is still drawable when
        // RootComponent, Mesh, bone-array pointer and world location agree.
        const auto root = is_player_actor_fingerprint(fingerprint) &&
            memory_.valid_pointer(prefetched.root)
            ? std::optional<std::uintptr_t>(prefetched.root)
            : read_root_component(memory_, actor, profile_.actor_root_component);
        const bool root_valid = root && memory_.valid_pointer(*root);
        if (root_valid) ++diagnostics.root_ok;
        const auto live_root_location = root_valid
            ? read_scene_location(memory_, *root, profile_.scene_component_location)
            : std::nullopt;
        const bool live_root_readable = live_root_location && live_root_location->finite() &&
            plausible_location(live_root_location->narrow());
        auto mesh_address = is_player_actor_fingerprint(fingerprint)
            ? prefetched.mesh : memory_.read<std::uintptr_t>(actor + profile_.character_mesh).value_or(0);
        bool mesh_valid = validate_skeletal_mesh(mesh_address);
        if (!mesh_valid) {
            mesh_address = resolve_actor_mesh(actor, local_mesh,
                                              actor_mesh_probe_budget, diagnostics);
            mesh_valid = validate_skeletal_mesh(mesh_address);
        }
        if (memory_.valid_pointer(mesh_address)) ++diagnostics.mesh_ok;
        const auto component = mesh_valid
            ? memory_.read<RemoteTransform96>(mesh_address + profile_.mesh_component_to_world) : std::nullopt;
        const bool component_valid = component && plausible_component_transform(*component);
        const bool live_root_valid = live_root_readable && component_valid &&
            root_matches_mesh(live_root_location, *component);
        const auto fallback_bone_array = mesh_valid && profile_.mesh_bone_array_fallback != 0
            ? memory_.read<std::uintptr_t>(mesh_address + profile_.mesh_bone_array_fallback) : std::nullopt;
        const bool fallback_bone_array_valid = fallback_bone_array && memory_.valid_pointer(*fallback_bone_array);
        const bool structure_candidate = mesh_valid && component_valid &&
            (profile_.mesh_bone_array != 0 || fallback_bone_array_valid);
        if (structure_candidate) ++diagnostics.structure_candidates;
        const bool identity_confirmed = current_pawn_matches ||
            (!current_pawn_mismatch && structure_candidate);
        if (mesh_valid && identity_confirmed) {
            Vec3 location{};
            bool have_location = false;
            // HX-42 reads Actor+0x1B0 to obtain RootComponent, then reads the
            // 24-byte LWC position at RootComponent+0x200 on every pass.
            if (live_root_valid) {
                location = live_root_location->narrow();
                have_location = true;
            } else if (component_valid) {
                location = {static_cast<float>(component->tx), static_cast<float>(component->ty),
                            static_cast<float>(component->tz) + kMeshOriginToCapsuleCenter};
                have_location = true;
            }
            if (have_location) {
                ++diagnostics.skeletal_mesh_ok;
                EntitySnapshot entity{};
                entity.actor = actor;
                entity.player_state = resolved_player_state;
                entity.team = resolved_team;
                populate_player_identity(entity);
                entity.root_component = profile_.actor_root_component != 0 && root_valid ? *root : 0;
                entity.mesh = mesh_address;
                entity.location = location;
                entity.distance_m = (entity.location - local.camera.location).length() / 100.0F;
                if (plausible_location(entity.location) && std::isfinite(entity.distance_m) && entity.distance_m <= 5000.0F) {
                    ++diagnostics.geometry_ok;
                    if (entity.root_component && profile_.allow_unverified_fields) {
                        if (const auto velocity = memory_.read<Vec3d>(*root + profile_.scene_component_velocity); velocity && velocity->finite())
                            entity.velocity = velocity->narrow();
                    }
                    const auto render_time = memory_.read<float>(mesh_address + profile_.mesh_visibility_time);
                    const auto now = std::chrono::steady_clock::now();
                    apply_render_visibility(entity, local, render_time, now);
                    const bool bones_read = read_bones(entity);
                    if (component_valid) {
                        const Vec3 mesh_center = mesh_capsule_center(*component);
                        const Vec3 motion_source = live_root_valid
                            ? live_root_location->narrow() : mesh_center;
                        entity.mesh_motion_fresh = update_mesh_motion_freshness(
                            actor, mesh_address, motion_source, now);
                        if (!live_root_valid ||
                            !align_to_live_root(entity, *component, live_root_location))
                            align_to_fresh_mesh(entity, *component,
                                                entity.visible || entity.mesh_motion_fresh);
                    }
                    entity.distance_m = (entity.location - local.camera.location).length() / 100.0F;
                    if (entity.visual_mesh_aligned) ++diagnostics.mesh_aligned_boxes;
                    if (entity.mesh_motion_fresh) ++diagnostics.mesh_motion_fresh;
                    if (entity.visible) ++diagnostics.render_fresh;
                    if (bones_read) ++diagnostics.bones_ok;
                    else ++diagnostics.root_position_only;
                    if (!resolved_player_state) ++diagnostics.accepted_fallback;
                    ++diagnostics.current_pawn_ok;
                    ++diagnostics.accepted_players;
                    out.push_back(entity);
                    continue;
                }
            }
        }

        if (mesh_valid && !current_pawn_mismatch && structure_candidate)
            ++diagnostics.structure_rejected;

        if (fingerprint != kWorldActorFingerprint) continue;
        const auto world_root = read_root_component(memory_, actor, profile_.actor_root_component);
        if (!world_root || !memory_.valid_pointer(*world_root)) continue;
        const auto location = read_scene_location(memory_, *world_root, profile_.scene_component_location);
        if (!location || !location->finite()) continue;
        EntitySnapshot entity{};
        entity.actor = actor;
        entity.root_component = *world_root;
        entity.world_actor = true;
        entity.location = location->narrow();
        entity.distance_m = (entity.location - local.camera.location).length() / 100.0F;
        if (!plausible_location(entity.location) || !std::isfinite(entity.distance_m) || entity.distance_m > 5000.0F) continue;
        ++diagnostics.geometry_ok;
        ++diagnostics.accepted_world;
        out.push_back(entity);
    }

    if (diagnostics.accepted_players > 0 || diagnostics.accepted_world > 0)
        diagnostics.stage = reported_actor_count > static_cast<std::int64_t>(actor_pointers.size())
            ? "complete_truncated" : "complete";
    else if (diagnostics.player_state_ok == 0 && diagnostics.structure_candidates == 0)
        diagnostics.stage = "player_state_unresolved";
    else if (diagnostics.geometry_ok == 0)
        diagnostics.stage = "geometry_unresolved";
    else
        diagnostics.stage = "all_candidates_filtered";
    diagnostics.actor_mesh_cached = static_cast<std::int32_t>(actor_mesh_cache_.size());
}

bool GameReader::refresh_local(FrameSnapshot& frame) {
    // Never refresh entity timestamps just because camera/local state is new.
    const double age=std::chrono::duration<double>(std::chrono::steady_clock::now()-frame.timestamp).count();
    LocalSnapshot local{};std::string error;
    if(!performance::reusable(age,frame.pid==memory_.pid()&&frame.module_base==memory_.module_base()) ||
       !memory_.alive() || !read_local(local,error) || local.world!=frame.local.world ||
       local.pawn!=frame.local.pawn || local.controller!=frame.local.controller ||
       local.player_state!=frame.local.player_state)return false;
    frame.local=local;
    return true;
}

FrameSnapshot GameReader::capture() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_remaining_=scan_budget_bytes_.load(std::memory_order_relaxed);
    FrameSnapshot frame;
    frame.timestamp = std::chrono::steady_clock::now();
    frame.sequence = ++sequence_;
    if (!ensure_attached(frame.error)) return frame;
    frame.pid = memory_.pid();
    frame.module_base = memory_.module_base();
    frame.module_name = memory_.module_name();
    if (!read_local(frame.local, frame.error)) {
        ++local_read_failures_;
        // Travel temporarily invalidates UWorld/controller chains. After a
        // short miss burst clear map-owned discoveries so recovery does not
        // keep stale island actors when the live world appears again.
        if (local_read_failures_ == 3) reset_world_state(0);
        return frame;
    }
    local_read_failures_ = 0;
    if (frame.local.world != observed_world_) reset_world_state(frame.local.world);
    scan_party_identity_cache();

    if (profile_.character_mesh == 0 || profile_.mesh_bone_array == 0 ||
        profile_.mesh_component_to_world == 0 || profile_.bone_stride != sizeof(RemoteTransform96)) {
        frame.entity_diagnostics.stage = "player_chain_pending";
        return frame;
    }
    const bool diagnostic_due=frame.timestamp>=next_diagnostic_probe_;
    if(diagnostic_due){
        next_diagnostic_probe_=frame.timestamp+std::chrono::milliseconds(500);
        cached_local_probe_={};cached_verified_probe_={};
        if(memory_.mapping_for(frame.local.pawn)){
            const auto local_mesh=memory_.read<std::uintptr_t>(frame.local.pawn+profile_.character_mesh).value_or(0);
            probe_mesh(local_mesh,frame.local,cached_local_probe_);
        }
    }
    frame.local_mesh_probe=cached_local_probe_;

    if (profile_.world_actor_array != 0) {
        // HX-42 reads the live UWorld actor TArray every frame. Do not merge
        // long-lived heap discoveries into this authoritative set: destroyed
        // Pawns can retain valid Mesh/Root data indefinitely after removal.
        read_entities(frame.local, frame.entities, frame.entity_diagnostics);
    } else {
        frame.entity_diagnostics.stage = "mesh_signature_recovery";
        discover_player_actors(frame.local, frame.entities, frame.entity_diagnostics);
        if (frame.entity_diagnostics.accepted_players <
            static_cast<std::int32_t>(kMaxDiscoveredMeshes)) {
            discover_mesh_entities(frame.local, frame.entities, frame.entity_diagnostics);
        }
    }
    scan_scoreboard_identity_cache(frame.local, frame.entities);
    frame.entity_diagnostics.actor_mesh_cached = static_cast<std::int32_t>(
        actor_mesh_cache_.size());
    if(diagnostic_due)for(const auto& entity:frame.entities){
        if(!entity.visible||!entity.mesh)continue;
        probe_mesh(entity.mesh,frame.local,cached_verified_probe_);break;
    }
    frame.verified_mesh_probe=cached_verified_probe_;
    return frame;
}

}  // namespace rebootnova
