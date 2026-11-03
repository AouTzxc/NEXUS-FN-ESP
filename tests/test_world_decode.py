from pathlib import Path
import configparser


def decrypt_world(encoded: int) -> int:
    mask = (1 << 64) - 1
    value = encoded ^ 0x00A047AB
    value = ((value >> 5) | (value << 59)) & mask
    return (0xB82BEA81 ^ value) * 0x6D9415B1BA6FABB9 & mask


def test_live_capture_vector():
    assert decrypt_world(0x23E1F16ADC09178C) == 0x7AF831A000


def decrypt_world_575(encoded: int) -> int:
    mask = (1 << 64) - 1
    value = (encoded + 0xFFFFFFFFFE7B8501) & mask
    value = ((value >> 61) | (value << 3)) & mask
    return 0x86213C84 ^ value


def test_cl575_live_capture_vector():
    assert decrypt_world_575(0x8000000DDEC76E8F) == 0x6E6C36A000


def test_profile_values():
    path = Path(__file__).parents[1] / "profiles" / "CL-56878558.ini"
    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    assert int(parser["verified"]["gworld"], 0) == 0x1DB20550
    assert int(parser["verified"]["camera_cache_size"], 0) == 0x34
    assert parser["target"]["build"] == "56878558"
    assert int(parser["verified"]["character_mesh"], 0) == 0x2E0
    assert int(parser["verified"]["mesh_bone_array"], 0) == 0x660
    assert int(parser["verified"]["mesh_component_to_world"], 0) == 0x1E0
    assert int(parser["verified"]["bone_stride"], 0) == 0x60
    # The device-tested AIM package enables the historical controller-angle
    # fields so the 0x4A0/0x4A8 memory writer can run on CL-56878558.
    assert parser["limits"]["allow_unverified_fields"] == "true"


def test_cl575_profile_values():
    path = Path(__file__).parents[1] / "profiles" / "CL-57566230.ini"
    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    assert parser["target"]["build"] == "57566230"
    assert int(parser["verified"]["gworld"], 0) == 0x1D590D20
    assert int(parser["verified"]["world_owning_game_instance"], 0) == 0x278
    assert int(parser["verified"]["world_actor_array"], 0) == 0x218
    assert int(parser["verified"]["level_actor_array"], 0) == 0x138
    assert int(parser["verified"]["actor_root_component"], 0) == 0x1B0
    assert int(parser["verified"]["scene_component_location"], 0) == 0x200
    assert int(parser["verified"]["mesh_bone_array"], 0) == 0x658
    assert int(parser["verified"]["mesh_visibility_time"], 0) == 0x294
    assert parser["limits"]["allow_unverified_fields"] == "false"


def decrypt_world_593(encoded: int) -> int:
    """CL-59343437 codec mode 3: bswap64(encoded ^ 0x1296A4C) - 0xF7FED09E."""
    mask = (1 << 64) - 1
    value = (encoded ^ 0x01296A4C) & mask
    swapped = int.from_bytes(value.to_bytes(8, "little"), "big")
    return (swapped - 0xF7FED09E) & mask


def encode_world_593(world: int) -> int:
    mask = (1 << 64) - 1
    swapped = int.from_bytes(((world + 0xF7FED09E) & mask).to_bytes(8, "little"), "big")
    return (swapped ^ 0x01296A4C) & mask


def test_cl593_live_capture_vector():
    # AGG live sample: module_base+0x1D92CF70 held 0x9EA204CB6F296A4C and the
    # decrypted UWorld was 0x6DD305D200.
    assert decrypt_world_593(0x9EA204CB6F296A4C) == 0x6DD305D200


def test_cl593_codec_round_trip():
    for world in (0, 1, 0x6DD305D200, 0x7AF831A000, (1 << 64) - 1):
        assert decrypt_world_593(encode_world_593(world)) == world


def test_cl593_cpp_codec_matches_python_reference():
    source = (Path(__file__).parents[1] / "jni/src/rebootnova/offset_profile.cpp").read_text(
        encoding="utf-8-sig")
    assert 'if (profile.build == "59343437")' in source
    assert "0x01296A4CULL" in source
    assert "0xF7FED09EULL" in source
    assert "OffsetProfile OffsetProfile::builtin_59343437()" in source
    assert "static OffsetProfile builtin_59343437();" in (
        Path(__file__).parents[1] / "jni/include/rebootnova/offset_profile.h"
    ).read_text(encoding="utf-8-sig")
    main = (Path(__file__).parents[1] / "jni/src/rebootnova/main.cpp").read_text(
        encoding="utf-8-sig")
    assert "OffsetProfile::builtin_59343437()" in main


def test_cl593_profile_values():
    path = Path(__file__).parents[1] / "profiles" / "CL-59343437.ini"
    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    verified = parser["verified"]
    assert parser["target"]["build"] == "59343437"
    expected = {
        "gworld": 0x1D92CF70,
        "world_owning_game_instance": 0x268,
        "world_actor_array": 0x208,
        "level_actor_array": 0x48,
        "game_instance_local_players": 0x38,
        "local_player_controller": 0x30,
        "controller_acknowledged_pawn": 0x308,
        "controller_camera_manager": 0x318,
        "pawn_player_state": 0x280,
        "player_state_team": 0xF49,
        "actor_class_fingerprint": 0x58,
        "actor_player_state": 0x280,
        "actor_root_component": 0x1B0,
        "character_mesh": 0x2E0,
        "scene_component_location": 0x200,
        "camera_cache": 0x1550,
        "camera_cache_size": 0x34,
        "mesh_bone_array": 0x650,
        "mesh_component_to_world": 0x1E0,
        "mesh_visibility_time": 0x338,
        "bone_stride": 0x60,
    }
    for key, value in expected.items():
        assert int(verified[key], 0) == value, key
    assert parser["limits"]["allow_unverified_fields"] == "false"


def test_cl593_profile_carries_only_esp_fields():
    # The server profile also reports name, scoreboard, model-chams,
    # vehicle-marker and bone-bank fields. This ESP build never reads them, so
    # they must not leak into the drawing profile.
    text = (Path(__file__).parents[1] / "profiles" / "CL-59343437.ini").read_text(
        encoding="utf-8-sig")
    body = "\n".join(line for line in text.splitlines() if not line.startswith("#"))
    for banned in ("scoreboard_", "model_", "entity_", "vehicle_marker", "account_",
                   "localized_name", "fingerprint_kind", "component_world_scale_delta",
                   "decode_mode", "decode_add", "decode_xor"):
        assert banned not in body, banned


def test_cl593_team_is_read_as_one_byte():
    reader = (Path(__file__).parents[1] / "jni/src/rebootnova/game_reader.cpp").read_text(
        encoding="utf-8-sig")
    helper = reader.split("int read_player_team(", 1)[1].split("}\n", 2)[0]
    assert 'profile.build == "59343437"' in helper
    assert "read<std::uint8_t>" in helper
    assert "*value < 255" in helper
    assert reader.count("read_player_team(memory_") == 4
