#include "rebootnova/process_memory.h"
#include "rebootnova/types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr const char* kPackage = "com.epicgames.fortnite";
constexpr const char* kModule = "libUnreal.so";
constexpr std::uint64_t kHvSignature = 3222016387ULL;
constexpr std::size_t kTextScanCap = 96U * 1024U * 1024U;
constexpr std::size_t kHvScanCap = 64U * 1024U * 1024U;
constexpr std::size_t kChunk = 1U * 1024U * 1024U;

struct RemoteArray {
    std::uintptr_t data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

bool plausible_ptr(const rebootnova::ProcessMemory& memory, std::uintptr_t value) {
    return memory.valid_pointer(value);
}

bool plausible_location(const rebootnova::Vec3d& value) {
    return value.finite() && std::abs(value.x) < 1.0e8 && std::abs(value.y) < 1.0e8 &&
           std::abs(value.z) < 1.0e8;
}

bool decode_adrp(std::uint32_t insn, std::uint64_t pc, std::uint32_t& rd, std::uint64_t& page) {
    if ((insn & 0x9F000000U) != 0x90000000U) return false;
    rd = insn & 0x1FU;
    std::int64_t immhi = (insn >> 5) & 0x7FFFF;
    std::int64_t immlo = (insn >> 29) & 0x3;
    std::int64_t imm = (immhi << 2) | immlo;
    if (imm & (1LL << 20)) imm |= ~((1LL << 21) - 1);
    page = (pc & ~0xFFFULL) + static_cast<std::uint64_t>(imm << 12);
    return true;
}

bool decode_ldr64(std::uint32_t insn, std::uint32_t& rt, std::uint32_t& rn, std::uint64_t& imm) {
    if ((insn & 0xFFC00000U) != 0xF9400000U) return false;
    rt = insn & 0x1FU;
    rn = (insn >> 5) & 0x1FU;
    imm = static_cast<std::uint64_t>((insn >> 10) & 0xFFFU) * 8U;
    return true;
}

std::uint64_t decrypt_575(std::uint64_t encoded) {
    const auto ror64 = [](std::uint64_t value, unsigned int count) {
        return (value >> count) | (value << (64U - count));
    };
    if (encoded & 0x8000000000000000ULL) {
        return 0x0000000086213C84ULL ^ ror64(encoded + 0xFFFFFFFFFE7B8501ULL, 61U);
    }
    return 0;
}

std::uint64_t decrypt_573(std::uint64_t encoded) {
    const auto ror64 = [](std::uint64_t value, unsigned int count) {
        return (value >> count) | (value << (64U - count));
    };
    return (0x00130AEE8D6352FBULL ^ ror64(encoded, 35U)) * 0x19C887B736261A09ULL;
}

struct WorldHit {
    std::uint64_t rva{};
    std::uint64_t encoded{};
    std::uintptr_t world{};
    const char* decrypt{"none"};
    int score{};
    std::uint64_t gi{};
    std::uint64_t levels{};
    std::uint64_t actors{};
    std::uint64_t players{};
    std::uint64_t controller{};
    std::uint64_t pawn{};
    std::uint64_t cam{};
    std::uint64_t cache{};
    std::uint64_t root{};
    std::uint64_t mesh{};
    std::uint64_t loc{};
    std::uint64_t bone{};
    std::uint64_t vis{};
    std::int32_t level_count{};
    std::int32_t actor_count{};
    std::uintptr_t local_pawn{};
    rebootnova::Vec3d location{};
    bool camera_ok{};
};

int score_array(const rebootnova::ProcessMemory& memory, std::uintptr_t address, int min_n, int max_n) {
    const auto array = memory.read<RemoteArray>(address);
    if (!array || !plausible_ptr(memory, array->data) || array->count < min_n ||
        array->count > max_n || array->capacity < array->count)
        return 0;
    return 1 + std::min(array->count, 8);
}

bool try_camera(const rebootnova::ProcessMemory& memory, std::uintptr_t manager, std::uint64_t cache) {
    rebootnova::CameraCacheRaw raw{};
    if (!memory.read_bytes(manager + cache, raw.bytes.data(), raw.bytes.size())) return false;
    double x{}, y{}, z{}, pitch{}, yaw{}, roll{};
    float fov{};
    std::memcpy(&x, raw.bytes.data() + 0, 8);
    std::memcpy(&y, raw.bytes.data() + 8, 8);
    std::memcpy(&z, raw.bytes.data() + 16, 8);
    std::memcpy(&pitch, raw.bytes.data() + 24, 8);
    std::memcpy(&yaw, raw.bytes.data() + 32, 8);
    std::memcpy(&roll, raw.bytes.data() + 40, 8);
    std::memcpy(&fov, raw.bytes.data() + 48, 4);
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::abs(x) < 1.0e8 && std::abs(y) < 1.0e8 && std::abs(z) < 1.0e8 &&
           std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(fov) &&
           fov >= 10.0F && fov <= 170.0F;
}

WorldHit walk_world(const rebootnova::ProcessMemory& memory, std::uintptr_t world,
                    std::uint64_t rva, std::uint64_t encoded, const char* decrypt) {
    WorldHit hit;
    hit.rva = rva;
    hit.encoded = encoded;
    hit.world = world;
    hit.decrypt = decrypt;
    constexpr std::array<std::uint64_t, 2> gi_off{0x270, 0x278};
    constexpr std::array<std::uint64_t, 2> levels_off{0x210, 0x218};
    constexpr std::array<std::uint64_t, 2> actors_off{0x40, 0x138};
    constexpr std::array<std::uint64_t, 1> players_off{0x38};
    constexpr std::array<std::uint64_t, 1> lp_off{0x30};
    constexpr std::array<std::uint64_t, 3> pawn_off{0x308, 0x2A0, 0x318};
    constexpr std::array<std::uint64_t, 3> cam_off{0x318, 0x320, 0x348};
    constexpr std::array<std::uint64_t, 3> cache_off{0x29D0, 0x2A00, 0x1FA0};
    constexpr std::array<std::uint64_t, 2> root_off{0x1B0, 0x158};
    constexpr std::array<std::uint64_t, 2> mesh_off{0x2E0, 0x310};
    constexpr std::array<std::uint64_t, 2> loc_off{0x200, 0x180};
    constexpr std::array<std::uint64_t, 2> bone_off{0x658, 0x660};

    std::uintptr_t game_instance = 0;
    std::uintptr_t controller = 0;
    for (auto gi : gi_off) {
        const auto game = memory.read<std::uintptr_t>(world + gi);
        if (!game || !plausible_ptr(memory, *game)) continue;
        for (auto players : players_off) {
            if (!score_array(memory, *game + players, 1, 8)) continue;
            const auto array = memory.read<RemoteArray>(*game + players);
            const auto local_player = memory.read<std::uintptr_t>(array->data);
            if (!local_player || !plausible_ptr(memory, *local_player)) continue;
            const auto found_controller = memory.read<std::uintptr_t>(*local_player + lp_off[0]);
            if (!found_controller || !plausible_ptr(memory, *found_controller)) continue;
            hit.gi = gi;
            hit.players = players;
            hit.controller = lp_off[0];
            hit.score += 6;
            game_instance = *game;
            controller = *found_controller;
            break;
        }
        if (controller) break;
    }
    if (!controller) return hit;

    int best_level_score = 0;
    for (auto levels : levels_off) {
        const int level_score = score_array(memory, world + levels, 1, 500);
        if (level_score <= best_level_score) continue;
        const auto levels_array = memory.read<RemoteArray>(world + levels);
        hit.levels = levels;
        hit.level_count = levels_array->count;
        best_level_score = level_score;
        const auto first_level = memory.read<std::uintptr_t>(levels_array->data);
        int best_actor_score = 0;
        hit.actors = 0;
        hit.actor_count = 0;
        if (first_level && plausible_ptr(memory, *first_level)) {
            for (auto actors : actors_off) {
                const int actor_score = score_array(memory, *first_level + actors, 8, 50000);
                if (actor_score <= best_actor_score) continue;
                const auto actors_array = memory.read<RemoteArray>(*first_level + actors);
                hit.actors = actors;
                hit.actor_count = actors_array->count;
                best_actor_score = actor_score;
            }
        }
        hit.score += level_score + best_actor_score;
    }

    for (auto pawn_delta : pawn_off) {
        const auto pawn = memory.read<std::uintptr_t>(controller + pawn_delta);
        if (!pawn || !plausible_ptr(memory, *pawn)) continue;
        hit.pawn = pawn_delta;
        hit.local_pawn = *pawn;
        hit.score += 4;
        for (auto root : root_off) {
            const auto root_ptr = memory.read<std::uintptr_t>(*pawn + root);
            if (!root_ptr || !plausible_ptr(memory, *root_ptr)) continue;
            bool located = false;
            for (auto loc : loc_off) {
                const auto pos = memory.read<rebootnova::Vec3d>(*root_ptr + loc);
                if (!pos || !plausible_location(*pos)) continue;
                hit.root = root;
                hit.loc = loc;
                hit.location = *pos;
                hit.score += 6;
                located = true;
                break;
            }
            if (located) break;
        }
        for (auto mesh : mesh_off) {
            const auto mesh_ptr = memory.read<std::uintptr_t>(*pawn + mesh);
            if (!mesh_ptr || !plausible_ptr(memory, *mesh_ptr)) continue;
            hit.mesh = mesh;
            hit.score += 3;
            for (auto bone : bone_off) {
                if (!score_array(memory, *mesh_ptr + bone, 1, 512)) continue;
                hit.bone = bone;
                hit.score += 4;
                break;
            }
            float vis_best = 0;
            std::uint64_t vis_off = 0;
            for (std::uint64_t off = 0x270; off <= 0x560; off += 4) {
                const auto value = memory.read<float>(*mesh_ptr + off);
                if (!value || !std::isfinite(*value) || *value <= 1.0F || *value > 1.0e7F)
                    continue;
                if (*value > vis_best) {
                    vis_best = *value;
                    vis_off = off;
                }
            }
            if (vis_off) {
                hit.vis = vis_off;
                hit.score += 2;
            }
            break;
        }
        break;
    }

    for (auto cam : cam_off) {
        const auto manager = memory.read<std::uintptr_t>(controller + cam);
        if (!manager || !plausible_ptr(memory, *manager)) continue;
        for (auto cache : cache_off) {
            if (!try_camera(memory, *manager, cache)) continue;
            hit.cam = cam;
            hit.cache = cache;
            hit.camera_ok = true;
            hit.score += 8;
            return hit;
        }
    }
    (void)game_instance;
    return hit;
}

void write_hex(FILE* out, const char* name, std::uint64_t value, const char* note) {
    if (value)
        std::fprintf(out, "%s=0x%llX  %s\n", name, static_cast<unsigned long long>(value), note);
    else
        std::fprintf(out, "%s=  %s\n", name, note);
}
}

int main(int argc, char** argv) {
    std::string out_path = "reboot-esp-offsets.txt";
    std::string package = kPackage;
    std::string module = kModule;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (arg == "--package" && i + 1 < argc) package = argv[++i];
        else if (arg == "--module" && i + 1 < argc) module = argv[++i];
        else if (arg == "--help") {
            std::fprintf(stderr, "usage: offset_dump [--out FILE] [--package NAME] [--module SO]\n");
            return 0;
        }
    }

    rebootnova::ProcessMemory memory;
    if (!memory.attach_by_package(package)) {
        std::fprintf(stderr, "attach failed: %s\n", memory.last_error().c_str());
        return 2;
    }
    if (!memory.refresh_module(module)) {
        std::fprintf(stderr, "module failed: %s\n", memory.last_error().c_str());
        return 3;
    }

    FILE* out = std::fopen(out_path.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 4;
    }

    const auto pid = memory.pid();
    const auto base = memory.module_base();
    std::fprintf(out, "REBOOT-ESP offset dump\n");
    std::fprintf(out, "package=%s\n", package.c_str());
    std::fprintf(out, "pid=%d\n", static_cast<int>(pid));
    std::fprintf(out, "module=%s\n", module.c_str());
    std::fprintf(out, "module_base=0x%llX\n", static_cast<unsigned long long>(base));
    std::fprintf(out, "note=ESP world-chain candidates only. Tracking/coloring RVAs are not recovered here.\n\n");

    const auto maps = memory.readable_mappings(pid);
    std::vector<rebootnova::MemoryRange> text;
    for (const auto& range : maps) {
        if (!range.executable || range.path.find(module) == std::string::npos) continue;
        text.push_back(range);
    }
    std::fprintf(out, "== libUnreal RX maps ==\n");
    for (const auto& range : text) {
        std::fprintf(out, "  0x%llX-0x%llX %s\n",
                     static_cast<unsigned long long>(range.start),
                     static_cast<unsigned long long>(range.end),
                     range.path.c_str());
    }

    std::vector<WorldHit> hits;
    std::size_t scanned = 0;
    std::vector<std::uint8_t> buffer(kChunk + 8);
    for (const auto& range : text) {
        if (scanned >= kTextScanCap) break;
        const auto span = std::min<std::uintptr_t>(range.end - range.start, kTextScanCap - scanned);
        for (std::uintptr_t address = range.start; address + 8 < range.start + span; address += kChunk) {
            const auto n = static_cast<std::size_t>(std::min<std::uintptr_t>(kChunk + 4, range.start + span - address));
            if (n < 8 || !memory.read_bytes(address, buffer.data(), n)) continue;
            scanned += n > 4 ? n - 4 : n;
            for (std::size_t off = 0; off + 8 <= n; off += 4) {
                std::uint32_t a{}, b{};
                std::memcpy(&a, buffer.data() + off, 4);
                std::memcpy(&b, buffer.data() + off + 4, 4);
                std::uint32_t rd{}, rt{}, rn{};
                std::uint64_t page{}, imm{};
                const std::uint64_t pc = address + off;
                if (!decode_adrp(a, pc, rd, page) || !decode_ldr64(b, rt, rn, imm) || rn != rd)
                    continue;
                const std::uint64_t gworld = page + imm;
                if (gworld < base || gworld - base > 0x40000000ULL) continue;
                const auto encoded = memory.read<std::uint64_t>(gworld);
                if (!encoded) continue;
                const std::uint64_t rva = gworld - base;
                const auto consider = [&](std::uintptr_t world, const char* name) {
                    if (!plausible_ptr(memory, world)) return;
                    auto hit = walk_world(memory, world, rva, *encoded, name);
                    if (hit.score <= 0) return;
                    auto existing = std::find_if(hits.begin(), hits.end(), [&](const WorldHit& h) {
                        return h.rva == rva && h.decrypt == name;
                    });
                    if (existing == hits.end()) hits.push_back(hit);
                    else if (hit.score > existing->score) *existing = hit;
                };
                consider(decrypt_575(*encoded), "575");
                consider(decrypt_573(*encoded), "573");
                if (plausible_ptr(memory, *encoded)) consider(*encoded, "raw");
            }
        }
    }
    std::sort(hits.begin(), hits.end(), [](const WorldHit& a, const WorldHit& b) {
        return a.score > b.score;
    });

    std::fprintf(out, "\n== GWorld candidates (text ADRP+LDR, scanned %zu bytes) ==\n", scanned);
    const std::size_t shown = std::min<std::size_t>(hits.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& hit = hits[i];
        std::fprintf(out,
                     "  #%zu score=%d decrypt=%s gworld_rva=0x%llX world=0x%llX encoded=0x%llX levels=%d actors=%d camera=%s pawn=0x%llX\n",
                     i + 1, hit.score, hit.decrypt,
                     static_cast<unsigned long long>(hit.rva),
                     static_cast<unsigned long long>(hit.world),
                     static_cast<unsigned long long>(hit.encoded),
                     hit.level_count, hit.actor_count,
                     hit.camera_ok ? "ok" : "no",
                     static_cast<unsigned long long>(hit.local_pawn));
    }
    if (hits.empty()) std::fprintf(out, "  none. Stay in a match, not the lobby, then rerun.\n");

    if (!hits.empty()) {
        const auto& best = hits.front();
        std::fprintf(out, "\n== Best ESP chain ==\n");
        write_hex(out, "gworld", best.rva, best.decrypt);
        write_hex(out, "world_owning_game_instance", best.gi, "UWorld -> GameInstance");
        write_hex(out, "world_actor_array", best.levels, "UWorld -> Levels TArray");
        write_hex(out, "level_actor_array", best.actors, "ULevel -> Actors TArray");
        write_hex(out, "game_instance_local_players", best.players, "GameInstance -> LocalPlayers");
        write_hex(out, "local_player_controller", best.controller, "ULocalPlayer -> Controller");
        write_hex(out, "controller_acknowledged_pawn", best.pawn, "Controller -> Pawn");
        write_hex(out, "controller_camera_manager", best.cam, "Controller -> CameraManager");
        write_hex(out, "camera_cache", best.cache, "CameraManager raw 0x34");
        write_hex(out, "actor_root_component", best.root, "Actor -> RootComponent");
        write_hex(out, "character_mesh", best.mesh, "Pawn -> Mesh");
        write_hex(out, "scene_component_location", best.loc, "RootComponent location");
        write_hex(out, "mesh_bone_array", best.bone, "Mesh -> Bone TArray");
        write_hex(out, "mesh_visibility_time", best.vis, "largest plausible LastRenderTime-like float");
        std::fprintf(out, "local_location=%.1f %.1f %.1f\n", best.location.x, best.location.y, best.location.z);
        std::fprintf(out, "\n# Candidate ini fragment. Confirm in-match before replacing builtin_57566230.\n");
        std::fprintf(out, "[target]\nbuild=unknown\npackage=%s\nmodule=%s\n\n[verified]\n",
                     package.c_str(), module.c_str());
        const auto line = [&](const char* name, std::uint64_t value) {
            if (value) std::fprintf(out, "%s=0x%llX\n", name, static_cast<unsigned long long>(value));
        };
        line("gworld", best.rva);
        line("world_owning_game_instance", best.gi);
        line("world_actor_array", best.levels);
        line("level_actor_array", best.actors);
        line("game_instance_local_players", best.players);
        line("local_player_controller", best.controller);
        line("controller_acknowledged_pawn", best.pawn);
        line("controller_camera_manager", best.cam);
        line("camera_cache", best.cache);
        line("actor_root_component", best.root);
        line("character_mesh", best.mesh);
        line("scene_component_location", best.loc);
        line("mesh_bone_array", best.bone);
        line("mesh_visibility_time", best.vis);
        std::fprintf(out, "camera_cache_size=0x34\nbone_stride=0x60\n");
    }

    std::fprintf(out, "\n== HV catalog (read-only, not an ESP offset) ==\n");
    std::fprintf(out, "signature_qword=%llu\n", static_cast<unsigned long long>(kHvSignature));
    std::fprintf(out, "legacy_patch_offsets=0x150C,0x1514,0x151C\n");
    std::size_t hv_scanned = 0;
    std::vector<std::uintptr_t> hv_hits;
    std::vector<std::uint8_t> hv_buf(kChunk + 8);
    for (const auto& range : maps) {
        if (hv_hits.size() >= 8 || hv_scanned >= kHvScanCap) break;
        if (!range.writable || !range.private_mapping || range.executable) continue;
        if (!(range.path.empty() || range.path.rfind("[anon:", 0) == 0 ||
              range.path == "[heap]" || range.path.find("dalvik") != std::string::npos))
            continue;
        const auto span = std::min<std::uintptr_t>(range.end - range.start, kHvScanCap - hv_scanned);
        for (std::uintptr_t address = range.start; address < range.start + span; address += kChunk) {
            const auto n = static_cast<std::size_t>(std::min<std::uintptr_t>(kChunk, range.start + span - address));
            if (n < 8 || !memory.read_bytes(address, hv_buf.data(), n)) continue;
            hv_scanned += n;
            for (std::size_t off = 0; off + 8 <= n; off += 4) {
                std::uint64_t value{};
                std::memcpy(&value, hv_buf.data() + off, 8);
                if (value != kHvSignature) continue;
                hv_hits.push_back(address + off);
                if (hv_hits.size() >= 8) break;
            }
            if (hv_hits.size() >= 8) break;
        }
    }
    std::fprintf(out, "scanned_bytes=%zu\nmatches_capped=%zu\n", hv_scanned, hv_hits.size());
    for (std::size_t i = 0; i < hv_hits.size(); ++i)
        std::fprintf(out, "  base[%zu]=0x%llX\n", i, static_cast<unsigned long long>(hv_hits[i]));
    if (hv_hits.size() < 4)
        std::fprintf(out, "status=legacy HV signature missing or incomplete; current silent aim will not bind this mode/build\n");
    else
        std::fprintf(out, "status=legacy HV signature still present; still not a structural offset dump\n");

    std::fprintf(out, "\n== Not recovered by this tool ==\n");
    std::fprintf(out, "tracking original_rva / code_rva / data_rva / vtable / probe / class RVAs\n");
    std::fprintf(out, "model-color function fingerprints and slack\n");
    std::fprintf(out, "new HV table layout if the 3222016387 catalog is gone\n");
    std::fprintf(out, "\nend\n");
    std::fclose(out);
    std::printf("%s\n", out_path.c_str());
    return hits.empty() ? 5 : 0;
}
