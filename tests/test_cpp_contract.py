from pathlib import Path


ROOT = Path(__file__).parents[1]
JNI = ROOT / "jni"
INCLUDE = JNI / "include/rebootnova"
SRC = JNI / "src/rebootnova"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_build_includes_silent_aim_and_excludes_bullet_track():
    make = read(JNI / "Android.mk")
    for unit in (
        "game_reader.cpp",
        "feature_engine.cpp",
        "v3_silent_manager.cpp",
        "segmented_recorder.cpp",
    ):
        assert unit in make
    for unit in ("model_chams_manager.cpp", "character_fill.cpp"):
        assert unit not in make
    assert not (SRC / "model_chams_manager.cpp").exists()
    assert not (SRC / "character_fill.cpp").exists()
    assert "bullet_track_manager.cpp" not in make
    assert not (SRC / "bullet_track_manager.cpp").exists()
    assert not (INCLUDE / "bullet_track_manager.h").exists()


def test_silent_aim_has_no_controller_angle_writer():
    source = read(SRC / "v3_silent_manager.cpp")
    header = read(INCLUDE / "v3_silent_manager.h")
    for token in (
        "update_angle",
        "pitch_offset",
        "yaw_offset",
        "kAimDeadzoneDegrees",
        "kNearTargetDegrees",
        "pitch_delta",
        "yaw_delta",
        "aim offsets pending",
        "active_pitch_address_",
        "CameraView",
    ):
        assert token not in source
        assert token not in header
    assert "void V3SilentManager::update_hv" in source
    assert "scan_hv_catalog" in source
    assert "controller_firing_state" not in source
    assert "controller_aiming_state" not in source


def test_hitbox_volume_is_removed_from_runtime_contract():
    combined = "\n".join(
        read(path) for path in (
            JNI / "Android.mk",
            SRC / "feature_engine.cpp",
            INCLUDE / "feature_engine.h",
            INCLUDE / "settings.h",
            SRC / "config_store.cpp",
            SRC / "ui.cpp",
        )
    )
    assert not (SRC / "hitbox_volume_manager.cpp").exists()
    assert not (INCLUDE / "hitbox_volume_manager.h").exists()
    for token in ("hitbox_volume", "HitboxVolume", "QWORD 90", "碰撞箱"):
        assert token not in combined


def test_silent_aim_is_active_without_unverified_profile_fields():
    feature = read(SRC / "feature_engine.cpp")
    start = feature.index("void FeatureEngine::update_v3_silent()")
    end = feature.index("void FeatureEngine::tick(", start)
    update = feature[start:end]
    assert "settings_.v3_silent_aim" in update
    assert "frame_.local.valid" in update
    assert "frame_.pid > 0" in update
    assert "frame_.local.world != 0" in update
    assert "allow_unverified_fields" not in update
    assert "!settings_.aim_enabled" not in update


def test_memory_aim_uses_explicit_input_state_and_reports_trigger_state():
    feature = read(SRC / "feature_engine.cpp")
    header = read(INCLUDE / "feature_engine.h")
    manager = read(SRC / "v3_aim_manager.cpp")
    assert "const bool touch_active" in feature
    assert "const bool gamepad_active" in feature
    assert "Touch::Down" not in feature
    assert "Touch::Move" not in feature
    assert "Touch::Up" not in feature
    assert "controller_aim_pitch_delta" in feature
    assert "aim offsets pending" in manager
    assert "sizeof(double)" in manager
    assert "aim_diagnostics_.firing = fire_touch_active" in feature
    assert "aim_diagnostics_.ads = ads_touch_active" in feature
    assert "aim_diagnostics_.trigger_active = trigger_active" in feature
    assert "settings_.bullet_track" not in feature
    for token in ("bool firing{}", "bool ads{}", "bool trigger_active{}"):
        assert token in header
    assert "bool firing{}" in read(INCLUDE / "game_reader.h")
    assert "bool aiming{}" in read(INCLUDE / "game_reader.h")


def test_live_actor_position_drives_all_player_paths_and_aim_yaw_axis():
    reader = read(SRC / "game_reader.cpp")
    aim = read(SRC / "v3_aim_manager.cpp")
    profile = read(ROOT / "profiles/CL-56878558.ini")
    for token in (
        "actor_class_fingerprint=0x58",
        "pawn_player_state=0x280",
        "actor_player_state=0x280",
        "player_state_team=0xF40",
        "actor_root_component=0x1B0",
        "scene_component_location=0x200",
    ):
        assert token in profile
    assert "read_root_component(memory_, entity.actor" in reader
    assert "read_scene_location(memory_, *root" in reader
    assert "const std::array<std::uint64_t, 3> offsets{primary_offset, 0x158, 0x1B0}" in reader
    assert "const std::array<std::uint64_t, 3> offsets{primary_offset, 0x180, 0x200}" in reader
    assert "root_matches_mesh(live_root_location, *component)" in reader
    assert "kRootMeshMaxSeparation = 400.0F" in reader
    assert "actor + kHxActorLocation" not in reader
    assert reader.count("align_to_live_root(") >= 4
    assert "const Vec3 motion_source = root_location->narrow()" in reader
    assert "const bool bones_read = read_bones(entity);" in reader
    assert "populated && plausible_location(bone)" in reader

    assert "current_pawn && *current_pawn != actor" in reader
    assert "std::atan2(static_cast<double>(delta.y), static_cast<double>(delta.x))" in aim
    assert "std::atan2(static_cast<double>(delta.x), static_cast<double>(delta.y))" not in aim
    assert "kNearTargetDegrees = 5.0" in aim
    assert "kAimDeadzoneDegrees = 0.02" in aim
    assert "angular_error / kNearTargetDegrees" in aim


def test_cl57316517_uses_live_mesh_visibility_timestamp():
    profile = read(ROOT / "profiles/CL-57316517.ini")
    implementation = read(SRC / "offset_profile.cpp")
    reader = read(SRC / "game_reader.cpp")
    assert "mesh_visibility_time=0x53C" in profile
    assert "profile.mesh_visibility_time = 0x53C;" in implementation
    assert "advances while rendered and freezes behind wall" in implementation
    assert "visibility_layout_changed" in reader
    assert "if (visibility_layout_changed) actor_temporal_.clear();" in reader


def test_live_world_actor_array_replaces_stale_heap_cache_for_death_and_respawn():
    reader = read(SRC / "game_reader.cpp")
    profile = read(ROOT / "profiles/CL-56878558.ini")
    assert "world_actor_array=0x210" in profile
    assert "level_actor_array=0x148" in profile
    assert "kDonorLevelCountLimit = 500" in reader
    assert "memory_.read<RemoteArray>(local.world + profile_.world_actor_array)" in reader
    assert "memory_.read<RemoteArray>(level + profile_.level_actor_array)" in reader
    assert "actor_pointers.reserve(static_cast<std::size_t>(kDonorActorCountLimit))" in reader
    assert "std::unordered_set<std::uintptr_t> unique_actors" in reader
    assert "!unique_actors.insert(actor).second" in reader
    assert "memory_.read_scatter(fingerprint_reads)" in reader
    assert "std::vector<std::pair<std::uintptr_t, std::uint64_t>> candidates" in reader
    assert "for (const auto& [actor, fingerprint] : candidates)" in reader
    assert "from tens of thousands of syscalls" in reader
    live_start = reader.index("if (profile_.world_actor_array != 0) {")
    live_end = reader.index("frame.entity_diagnostics.actor_mesh_cached", live_start)
    live_path = reader[live_start:live_end]
    assert "read_entities(frame.local, frame.entities" in live_path
    assert "discover_player_actors" in live_path
    assert live_path.index("} else {") < live_path.index("discover_player_actors")
    assert "destroyed\n        // Pawns can retain valid Mesh/Root data indefinitely" in reader
    assert "constexpr std::uint16_t kMeshCacheMissGrace = 180" in reader


def test_reader_reprobes_package_and_resets_process_caches_on_pid_change():
    reader = read(SRC / "game_reader.cpp")
    header = read(INCLUDE / "game_reader.h")
    memory = read(SRC / "process_memory.cpp")
    assert "std::chrono::milliseconds(500)" in reader
    assert "attach_by_package(profile_.package_name)" in reader
    assert "reset_process_state(memory_.pid())" in reader
    assert "memory_.alive() && memory_.module_base()" in reader
    assert "refresh_module_candidates(candidates)" in reader
    assert "before_module != memory_.module_base()" in reader
    assert "reset_world_state(frame.local.world)" in reader
    assert "local_read_failures_ == 3" in reader
    assert "UWorld changed; map caches reset" in reader
    assert "actor_mesh_cache_.clear()" in reader
    assert "discovered_meshes_.clear()" in reader
    assert "actor_temporal_.clear()" in reader
    assert "next_process_probe_" in header
    assert "observed_world_" in header
    assert "local_read_failures_" in header
    assert "const bool mapped_target = has_unreal && maps_package" in memory
    assert "(mapped_target ? 1000 : 0)" in memory
    assert "target process not found package=" in memory
    assert "maps_open_fail=" in memory
    assert "Fast path: only processes whose cmdline names the requested package" in memory
    assert "if (!exact && !child) continue;" in memory
    assert "if (found <= 0 && cmdline_candidates == 0)" in memory
    assert "Parsing every process" in memory
    assert 'map_line.find("libUnreal.so")' in memory
    assert "map_line.find(package)" in memory
    assert "candidate_pid == pid_.load(std::memory_order_relaxed) ? 1 : 0" in memory


def test_aim_activation_uses_only_selected_touch_or_gamepad_input():
    feature = read(SRC / "feature_engine.cpp")
    header = read(INCLUDE / "feature_engine.h")
    settings = read(INCLUDE / "settings.h")
    config = read(SRC / "config_store.cpp")
    ui = read(SRC / "ui.cpp")
    touch_header = read(ROOT / "third_party/surface_imgui/jni/include/Android_touch/TouchHelperA.h")
    touch_source = read(ROOT / "third_party/surface_imgui/jni/src/Android_touch/TouchHelperA.cpp")
    assert "aim_base_fov_" in feature
    assert "bool FeatureEngine::update_fov_ads" in feature
    assert "aim_fov_drop_samples_ >= 1" in feature
    assert "one sampled jump" in feature
    assert "kStableRecalibration = std::chrono::milliseconds(300)" in feature
    assert "kMaximumAdsTransition = std::chrono::milliseconds(700)" in feature
    assert "aim_fov_ads_floor_" in feature
    assert "release_gain = current_fov - aim_fov_ads_floor_" in feature
    assert "aim_fov_recovery_samples_ >= 1" in feature
    assert "aim_fov_baseline_ready_" in feature
    assert "kBaselineWarmup = std::chrono::milliseconds(600)" in feature
    assert "Require a stable hip baseline first" in feature
    assert "A stable low FOV without a confirmed multi-frame zoom transition" in feature
    assert "reset_aim_fov_calibration" in header
    assert "aim_ads_fov_drop_percent{10.0F}" in settings
    assert '"aim_ads_fov_drop_percent"' in config
    update_aim = feature.split("void FeatureEngine::update_aim", 1)[1].split(
        "void FeatureEngine::tick", 1
    )[0]
    assert 'full_width_combo("##aim_input_mode"' in ui
    assert 'tr(language, "触控", "Touch")' in ui
    assert 'tr(language, "手柄", "Gamepad")' in ui
    assert '"重新校准腰射 FOV", "Recalibrate Hip-fire FOV"' not in ui
    assert '"ADS FOV 降幅", "ADS FOV drop"' not in ui
    assert "aim_ads_fov_threshold" not in feature + settings + config + ui
    assert "const bool touch_regions_enabled = !settings_.aim_touch_region_editing" in feature
    assert "Touch::HasPhysicalTouchInNormalizedRegion" in feature
    for token in (
        "settings_.aim_fire_region_min_x", "settings_.aim_fire_region_min_y",
        "settings_.aim_fire_region_max_x", "settings_.aim_fire_region_max_y",
        "settings_.aim_ads_region_min_x", "settings_.aim_ads_region_min_y",
        "settings_.aim_ads_region_max_x", "settings_.aim_ads_region_max_y",
    ):
        assert token in feature
    assert "const bool touch_active = fire_touch_active || ads_touch_active || converter_keys != 0;" in feature
    assert "aim_diagnostics_.firing = fire_touch_active" in feature
    assert "aim_diagnostics_.ads = ads_touch_active" in feature
    assert "const bool trigger_active = settings_.aim_gamepad_enabled" in feature
    assert "? gamepad_active" in feature
    assert ": touch_active" in feature
    assert "const bool fov_ads = update_fov_ads" not in update_aim
    assert "switch (settings_.aim_trigger)" not in update_aim
    assert 'aim_diagnostics_.state = "waiting for activation"' in feature
    assert "!settings_.aim_touch_region_editing" in feature
    assert "HasPhysicalStationaryTouchInNormalizedRegion" in touch_header
    assert "float max_motion_px = 18.0F" in touch_header
    assert "std::hypot(position.x - start->second.x" in touch_source
    assert "kMinimumHold = std::chrono::milliseconds(35)" in touch_source
    assert "resetPhysicalTouchTracking()" in touch_source
    assert touch_source.count("resetPhysicalTouchTracking();") >= 2
    assert "Finger[9]" in touch_source
    assert "Touch::HasPhysicalTouchInNormalizedRegion" in feature
    assert "Boxed HUD buttons: any contact inside the fire/ADS region is a press." in feature
    assert "bool aim_converter_mapped_touch{false}" in read(INCLUDE / "settings.h")


def test_root_km_activator_uses_eviocgkey_and_all_hid():
    converter = read(SRC / "converter_input.cpp")
    touch_source = read(ROOT / "third_party/surface_imgui/jni/src/Android_touch/TouchHelperA.cpp")
    ui = read(SRC / "ui.cpp")
    assert "EVIOCGKEY(snap.bits.size())" in converter
    assert "selector.empty() || n.description.selector==selector" in converter
    assert "(selector.empty()&&nodes.size()==1)" not in converter
    assert "hotplugTouchDevices" in touch_source
    assert "selfUinputName" in touch_source
    assert "selfEventPath" in touch_source
    assert "rememberSelfEventPath" in touch_source
    assert "devices.reserve(16)" in touch_source
    assert "Listen only. Never grab hotplugged nodes" in touch_source
    hotplug = touch_source.split("static void hotplugTouchDevices()", 1)[1].split("void UpdateImGui()", 1)[0]
    assert "EVIOCGRAB" not in hotplug
    assert "device.grabbed = false" in hotplug
    assert 'tr(language,"全部 HID（root 激活器）","All HID (root activator)")' in ui


def test_touch_look_aim_rewrites_grabbed_finger_not_finger9():
    feature = read(SRC / "feature_engine.cpp")
    header = read(INCLUDE / "feature_engine.h")
    settings = read(INCLUDE / "settings.h")
    config = read(SRC / "config_store.cpp")
    ui = read(SRC / "ui.cpp")
    touch_header = read(ROOT / "third_party/surface_imgui/jni/include/Android_touch/TouchHelperA.h")
    touch_source = read(ROOT / "third_party/surface_imgui/jni/src/Android_touch/TouchHelperA.cpp")
    update_aim = feature.split("void FeatureEngine::update_aim", 1)[1].split(
        "void FeatureEngine::tick", 1
    )[0]
    assert "bool aim_touch_output{true}" in settings
    assert settings.rfind("bool aim_touch_output{true}") > settings.rfind("int tracking_unhook_idle_ms")
    assert "Keep extension fields append-only" in settings
    assert "RN_BOOL(aim_touch_output)" in config
    assert "RN_LOAD_BOOL(aim_touch_output)" in config
    assert "struct LookAimCommand" in touch_header
    assert "void SetLookAim(const LookAimCommand& cmd)" in touch_header
    assert "bool grabbed{}" in touch_header
    assert "applyLookAim();" in touch_source
    apply_idx = touch_source.find("applyLookAim();")
    upload_idx = touch_source.find("Upload();", apply_idx)
    assert apply_idx != -1 and upload_idx != -1
    assert "if (!device.grabbed) continue" in touch_source
    assert "device.grabbed = !readOnly && !converter_bus" in touch_source
    assert "Touch::SetLookAim" in update_aim
    assert "Touch::Down" not in feature
    assert "Touch::Move" not in feature
    assert "Touch::Up" not in feature
    assert "std::isfinite(best_screen.x)" in update_aim
    assert "bool touch_output{}" in header
    assert "aim_diagnostics_.touch_output" in update_aim
    assert 'tr(language, "触摸搓屏输出", "Touch look output")' in ui
    assert "settings_.aim_touch_output" in ui
    assert "settings_.aim_touch_output" in update_aim


def test_aim_lock_stays_on_first_actor_until_release_or_sustained_loss():
    feature = read(SRC / "feature_engine.cpp")
    header = read(INCLUDE / "feature_engine.h")
    update_aim = feature.split("void FeatureEngine::update_aim", 1)[1].split(
        "void FeatureEngine::tick", 1
    )[0]
    assert "aim_lock_last_valid_" in header
    assert "kTargetLossGrace = std::chrono::milliseconds(250)" in update_aim
    assert "if (locked_candidate)" in update_aim
    assert "best = locked_candidate" in update_aim
    assert "now - aim_lock_last_valid_ < kTargetLossGrace" in update_aim
    assert 'aim_diagnostics_.state = "holding target"' in update_aim
    holding = update_aim.split('aim_diagnostics_.state = "holding target"', 1)[0]
    assert "disable_angle_writers();" in holding[-500:]
    assert "const auto clear_aim_lock" in update_aim
    assert "if (!trigger_active)" in update_aim
    release = update_aim.split("if (!trigger_active)", 1)[1].split("const Vec2 center", 1)[0]
    assert "clear_aim_lock();" in release
    assert "distance < best_screen_distance" in update_aim
    assert "distance <= best_screen_distance" not in update_aim


def test_touch_aim_has_independent_fire_and_ads_regions_with_old_config_fallback():
    settings = read(INCLUDE / "settings.h")
    config = read(SRC / "config_store.cpp")
    ui = read(SRC / "ui.cpp")
    fields = (
        "aim_ads_region_min_x", "aim_ads_region_min_y",
        "aim_ads_region_max_x", "aim_ads_region_max_y",
    )
    for field in fields:
        assert field in settings
        assert f'"{field}"' in config
    assert settings.index("int aim_gamepad_button2") < settings.index("float aim_ads_region_min_x")
    assert "Keep extension fields append-only" in settings
    assert "void Ui::begin_touch_region_editor(bool ads_region)" in ui
    assert "begin_touch_region_editor(false)" in ui
    assert "begin_touch_region_editor(true)" in ui
    assert "touch_region_editing_ads_" in read(INCLUDE / "ui.h")
    assert '"Adjust Fire Region"' in ui
    assert '"Adjust ADS Region"' in ui


def test_runtime_status_is_compact_and_scrollbar_is_touch_friendly():
    ui = read(SRC / "ui.cpp")
    status = ui.split("void Ui::status_tab", 1)[1].split("void Ui::esp_tab", 1)[0]
    for token in ("Renderer", "FPS:", "Touch", "PID:", "Module:", "Game status", "Entities", "Build / CL"):
        assert token in status
    for token in (
        "V3 Silent", "Actor:", "Projection:", "Mesh signatures",
        "Local probe", "Dynamic probe", "Stage:", "Reader", "action_status",
    ):
        assert token not in status
    assert status.count("ImGui::Text(") == 8
    assert "style.ScrollbarSize = 28.0F" not in ui
    assert "const float detail_scrollbar_size = std::clamp(" in ui
    assert "64.0F * ui_scale_, 56.0F, 72.0F" in ui
    assert "ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, detail_scrollbar_size)" in ui
    assert "ImGuiCol_ScrollbarGrab" in ui
    assert '"Process and rendering status"' in ui


def test_gamepad_devices_and_two_aim_bindings_use_nonblocking_evdev():
    feature = read(SRC / "feature_engine.cpp")
    header = read(INCLUDE / "feature_engine.h")
    settings = read(INCLUDE / "settings.h")
    config = read(SRC / "config_store.cpp")
    ui = read(SRC / "ui.cpp")
    for token in (
        "aim_gamepad_enabled", "aim_gamepad_device",
        "aim_gamepad_button1", "aim_gamepad_button2",
    ):
        assert token in settings
        assert token in config
    assert settings.index("OffscreenLayout offscreen_layout") < settings.index("bool aim_gamepad_enabled")
    assert "Keep extension fields append-only" in settings
    assert "class GamepadManager" in feature
    assert "class GamepadPollingBackend" in feature
    assert "worker_ = std::thread(&GamepadManager::worker_loop, this)" in feature
    constructor = feature[feature.index("FeatureEngine::FeatureEngine"):feature.index("FeatureEngine::~FeatureEngine")]
    assert "make_unique<GamepadManager>" not in constructor
    assert "capture_worker_ = std::thread(&FeatureEngine::capture_loop, this)" in constructor
    assert "if (!gamepads_ && settings_.aim_gamepad_enabled)" in feature
    assert "wake_.wait(lock, wake_requested)" in feature
    assert "backend_->suspend()" in feature
    assert "std::atomic_bool active_" in feature
    assert "std::try_to_lock" in feature
    assert 'opendir("/dev/input")' in feature
    assert '"/dev/input/" + name' in feature
    assert "O_RDONLY | O_NONBLOCK | O_CLOEXEC" in feature
    assert "EVIOCGBIT(EV_KEY" in feature
    assert "EVIOCGBIT(EV_ABS" in feature
    assert "bit_set(abs_bits, ABS_X)" in feature
    assert "bit_set(abs_bits, ABS_Y)" in feature
    assert "!bit_set(key_bits, BTN_TOUCH)" in feature
    assert "key_count >= 4" in feature
    assert "kMaximumBatchesPerFrame = 4" in feature
    assert "std::chrono::seconds(5)" in feature
    assert "last_enumeration_ = std::chrono::steady_clock::now()" in feature
    assert "id.bustype == 0x05" in feature
    assert "EVIOCGUNIQ" in feature
    assert "EVIOCGPHYS" in feature
    assert "ABS_Z" in feature and "ABS_RZ" in feature
    assert "settings_.aim_gamepad_button1" in feature
    assert "settings_.aim_gamepad_button2" in feature
    assert "const bool trigger_active = settings_.aim_gamepad_enabled" in feature
    assert "GamepadDeviceInfo" in header
    assert "GamepadDiagnostics" in header
    assert 'begin_card("##aim_gamepad")' in ui
    aim = ui[ui.index("void Ui::aim_detail()"):ui.index("void Ui::silent_aim_detail()")]
    assert 'full_width_combo("##aim_input_mode"' in aim
    assert 'bool_mode_selector("##aim_input_mode"' not in aim
    assert '"刷新设备", "Refresh devices"' in ui
    assert '"绑定按键 1", "Bind button 1"' in ui
    assert '"绑定按键 2", "Bind button 2"' in ui
    assert "features_.begin_gamepad_binding(1)" in ui
    assert "features_.begin_gamepad_binding(2)" in ui


def test_aim_writes_each_fresh_camera_sample_once_and_filters_bone_jitter():
    manager = read(SRC / "v3_aim_manager.cpp")
    header = read(INCLUDE / "v3_aim_manager.h")
    assert "std::uint64_t sequence{}" in header
    assert "std::uint64_t next_sequence_{}" in header
    assert "next.sequence = ++next_sequence_" in manager
    assert "requested_.sequence != processed_sequence" in manager
    assert "processed_sequence = request.sequence" in manager
    assert "wait_for" not in manager
    assert "next.target.z = requested_.target.z" in manager
    assert "* 0.30F" in manager


def test_ui_exposes_silent_aim_control_in_separate_aim_module():
    ui = read(SRC / "ui.cpp")
    aim_start = ui.index("void Ui::silent_aim_detail()")
    misc_start = ui.index("void Ui::misc_detail()")
    aim = ui[aim_start:misc_start]
    assert 'begin_card("##aim_silent")' in aim
    assert 'ImGui::Checkbox(tr(language, "静默自瞄", "Silent Aim")' in aim
    assert 'full_width_slider_float("##silent_scale_slider"' in aim
    assert "&slider_scale, 1.0F, 50.0F" in aim
    assert 'ImGui::InputFloat("##silent_custom_scale"' in aim
    assert "std::clamp(custom_scale, 1.0F, 1000000.0F)" in aim
    assert '"1200 ms"' in aim
    assert ui.count('"Silent Aim"') >= 1
    assert "Bullet Tracking" in ui
    assert "begin_card(\"##misc_v3_silent\")" not in ui


def test_aim_touch_region_has_fullscreen_editor_and_persistent_bounds():
    ui = read(SRC / "ui.cpp")
    ui_header = read(INCLUDE / "ui.h")
    settings = read(INCLUDE / "settings.h")
    config = read(SRC / "config_store.cpp")
    feature = read(SRC / "feature_engine.cpp")
    for token in (
        "aim_fire_region_min_x", "aim_fire_region_min_y",
        "aim_fire_region_max_x", "aim_fire_region_max_y",
    ):
        assert token in settings
        assert token in config
        assert token in feature
    for token in (
        '"自瞄触控区域", "AIM Touch Region"',
        "void Ui::touch_region_detail()",
        "void Ui::render_touch_region_editor()",
        'ImGui::Begin("##AIMTouchRegionEditor"',
        'ImGui::InvisibleButton("##move_fire_region"',
        'resize_corner("##fire_region_tl"',
        'tr(language, "保存", "Save")',
        'tr(language, "取消", "Cancel")',
        'tr(language, "重置", "Reset")',
        "config_.save(settings_, &error)",
        "touch_region_editing_ = false",
    ):
        assert token in ui
    assert "touch_region_editing_" in ui_header
    assert "settings_.aim_touch_region_editing = true" in ui
    assert ui.count("settings_.aim_touch_region_editing = false") >= 2
    assert "const int selected = std::clamp(selected_modules_[category_], 0, build_variant::aim_last_module);" in ui
    assert "if (selected == 1) silent_aim_detail();" in ui
    assert "else if (selected == 2) touch_region_detail();" in ui


def test_silent_aim_uses_verified_hv_catalog_and_restores_it():
    manager = read(SRC / "v3_silent_manager.cpp")
    header = read(INCLUDE / "v3_silent_manager.h")
    for token in (
        "kLuaQwordSignature = 3222016387ULL",
        "kLuaPatchOffsets{0x150C, 0x1514, 0x151C}",
        "kRequiredBases = 4U",
        "scan_hv_catalog",
        "apply_hv",
        "restore_hv",
        "std::uint64_t original_bits{}",
        "std::uint64_t applied_bits{}",
        "anonymous_candidate",
        "std::uint32_t))",
    ):
        assert token in manager or token in header
    assert "restore_hv();" in manager


def test_bundled_font_bakes_all_subset_chinese_glyphs():
    loader = read(ROOT / "third_party/surface_imgui/jni/src/Android_my_imgui/my_imgui.cpp")
    subset_tool = read(ROOT / "tools/subset_font.py")
    assert "builder.AddRanges(GetGlyphRangesChineseSimplifiedOfficial())" in loader
    assert "builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull())" not in loader
    assert "collect_official_simplified_codepoints" in subset_tool
    assert 'instantiateVariableFont(font, {"wght": 500}' in subset_tool
    assert "output_font.getBestCmap()" in subset_tool
    assert "subset font is missing requested glyphs" in subset_tool


def test_visible_only_aim_rejects_unknown_visibility_samples():
    feature = read(SRC / "feature_engine.cpp")
    assert "if (visible_only && (!entity.visibility_known || !entity.visible)) continue;" in feature


def test_cl575_visibility_identity_cache_and_ordinary_aim_master():
    profile = read(ROOT / "profiles/CL-57566230.ini")
    feature = read(SRC / "feature_engine.cpp")
    reader = read(SRC / "game_reader.cpp")
    assert "mesh_visibility_time=0x294" in profile
    assert "if (!settings_.aim_enabled || !frame_.local.valid)" in feature
    assert "Ordinary AIM is the only caller allowed to request controller rotation" in feature
    assert "player_identity_cache_" in reader
    assert "std::chrono::seconds(30)" in reader
    assert "std::array<std::uint8_t, 0x700> state" in reader
    assert "next_platform_probe_" in reader


def test_cl575_party_cache_resolves_real_display_name_incrementally():
    reader = read(SRC / "game_reader.cpp")
    header = read(INCLUDE / "game_reader.h")
    assert "player_state + 0x278" in reader
    assert "*identity + 0x10" in reader
    assert '"\\\"account_id\\\":\\\""' in reader
    assert '"\\\"account_dn\\\":\\\""' in reader
    assert "kPartyIdentityScanChunk = 64U * 1024U" in reader
    assert "kPartyIdentityScanLimit = 64U * 1024U * 1024U" in reader
    assert 'range.path.find("libc_malloc")' in reader
    assert "return a.start < b.start;" in reader
    assert "case 'u':" in reader
    assert "0x10000u" in reader
    assert "account_display_names_" in header


def test_cl575_scoreboard_rows_bind_real_names_to_live_player_states():
    reader = read(SRC / "game_reader.cpp")
    header = read(INCLUDE / "game_reader.h")
    assert "kScoreboardScanChunk = 256U * 1024U" in reader
    assert "kScoreboardRescanInterval = std::chrono::seconds(5)" in reader
    assert "*history + 0x20" in reader
    assert "*history + 0x28" in reader
    assert "row + 0x30" in reader
    assert "*tag != 0x12U" in reader
    assert "live_states.find(*state) == live_states.end()" in reader
    assert "scoreboard_display_names_[state]" in reader
    assert "scan_scoreboard_identity_cache(frame.local, frame.entities)" in reader
    assert "scoreboard_display_names_.clear()" in reader
    assert "scoreboard_scan_ranges_" in header
    assert "kScoreboardStateLookbehind = 32ULL * 1024ULL * 1024ULL" in reader
    assert "kScoreboardHintWindow = 32ULL * 1024ULL * 1024ULL" in reader
    assert "scoreboard_scan_hint_" in reader
    assert "state - kScoreboardStateLookbehind" in reader
    assert 'runtime::log("scoreboard name=%s ps=0x%llX row=0x%llX"' in reader
    assert "read_localized_player_name" in reader
    assert "localized_name_fallback" in reader
    assert "return byte >= 0x80;" in reader
    assert "cached.player_name = read_localized_player_name(memory_, entity.player_state);" in reader
    assert "opaque ASCII strings" in reader
    assert "{0x300ULL, 0x2F0ULL}" in reader
    assert "scoreboard_display_names_.erase(live_state);" not in reader
    assert "cp == 0xfffdu" in reader
    assert "if (low < 0xdc00 || low > 0xdfff) return {};" in reader
    feature = read(SRC / "feature_engine.cpp")
    subset = read(ROOT / "tools/subset_font.py")
    assert 'std::snprintf(identity, sizeof(identity), "UID:%s [%s]"' not in feature
    assert "return !entity.player_name.empty();" in feature
    assert "return !entity.player_name.empty() || !entity.player_id.empty();" not in feature
    assert 'runtime::log("identity resolved ps=0x%llX account=%s name=%s"' in reader
    assert "range(0x3041, 0x3097)" in subset
    assert "range(0x31F0, 0x3200)" in subset


def test_scoreboard_names_are_persisted_by_account_id_for_modes_without_rows():
    reader = read(SRC / "game_reader.cpp")
    header = read(INCLUDE / "game_reader.h")
    assert '"/data/local/tmp/reboot-esp/account-names.tsv"' not in reader
    assert "persistent_account_display_names_" in header
    assert "remember_scoreboard_name(state, name)" in reader
    assert "account_id = read_player_id(memory_, state)" in reader
    assert "persistent_account_display_names_.find(cached.player_id)" in reader


def test_online_name_lookup_is_manual_background_fallback():
    settings = read(INCLUDE / "settings.h")
    feature_header = read(INCLUDE / "feature_engine.h")
    reader_header = read(INCLUDE / "game_reader.h")
    feature = read(SRC / "feature_engine.cpp")
    reader = read(SRC / "game_reader.cpp")
    ui = read(SRC / "ui.cpp")
    config = read(SRC / "config_store.cpp")
    assert "bool online_name_lookup{false}" in settings
    assert "OnlineNameDiagnostics" in feature_header
    assert "OnlineNameResolver" in feature_header
    assert "worker_loop" in feature
    assert '"/system/bin/curl"' in feature
    assert 'const_cast<char*>("@-")' in feature
    assert '"Authorization: Bearer " + token' in feature
    assert "online_names_->apply(settings_.online_name_lookup" in feature
    assert 'tr(language, "联网昵称查询", "Online Name Lookup")' in ui
    assert "仅当昵称不正确时手动开启" in ui
    assert "RN_BOOL(online_name_lookup)" in config
    assert "RN_LOAD_BOOL(online_name_lookup)" in config
    assert "if (enabled_ || pending_ || diagnostics_.busy) ++generation_;" in feature
    assert "generation = generation_;" in feature
    assert "generation_ != generation" in feature
    assert 'kJwtPrefix{"eyJ"}' in feature
    assert "looks_like_epic_account_token" in feature
    assert 'payload->find("\\\"clsvc\\\":")' in feature
    assert "find_access_tokens" in feature
    assert 'kOAuthPrefix{"eg1~"}' in feature
    assert "kCandidateLimit = 96" in feature
    assert "if (attempt.first == 200)" in feature
    assert "next_retry_" in feature
    assert "clear_access_token_locked" in feature
    assert "cached_rejected" in feature
    assert "response.first == 401 || response.first == 403" in feature
    assert "expiry <= now + 30" in feature
    assert "new_unknown_id" in feature
    assert "seen_ids_.insert(id).second" in feature
    assert "seen_ids_.clear()" in feature
    assert "kScanWindow = 64U * 1024U * 1024U" in feature
    assert "kFastScanLimit = 1024U * 1024U * 1024U" in feature
    assert "kMaintenanceScanLimit = 512U * 1024U * 1024U" in feature
    assert "kFastScanAttempts = 8" in feature
    assert "std::chrono::milliseconds(750)" in feature
    assert "scan_window_cursor_" in feature
    assert "size > 4ULL * 1024ULL * 1024ULL * 1024ULL" in feature
    assert "new_unknown_id && !diagnostics_.busy && !access_token_.empty()" in feature
    assert '"waiting for session retry"' in feature
    assert "等待游戏会话，自动重试中" in ui
    assert '"联网昵称查询", "Online Name Lookup"' in ui
    assert 'std::clamp(selected_modules_[2], 0, 4)' in ui
    assert '"Accent and rainbow"), nullptr' in ui
    assert "if (settings.rainbow)" in ui
    assert "bool_mode_selector" in ui
    assert 'tr(language, "强调色模式已保存", "Accent mode saved")' in ui
    assert "if (settings.rainbow)" not in read(SRC / "main.cpp")
    assert '"ONLINE_NAME state=%s http=%d resolved=%zu requested=%zu\\n"' in feature
    assert "take_updates()" in feature
    assert "pending_external_names_" in feature
    assert "reader_.remember_external_names(names)" in feature
    assert "remember_external_names" in reader_header
    assert 'runtime::log("online name cache saved added=%zu total=%zu"' in reader


def test_visibility_palette_maps_unknown_samples_to_occluded_color():
    feature = read(SRC / "feature_engine.cpp")
    assert "const bool confirmed_visible = entity.visibility_known && entity.visible;" in feature
    assert "? (confirmed_visible ? settings_.chams_visible : settings_.chams_hidden)" in feature
    assert "settings_.visibility_colors && entity.visibility_known" not in feature


def test_drawing_visibility_uses_local_render_clock():
    reader = read(SRC / "game_reader.cpp")
    header = read(INCLUDE / "game_reader.h")
    assert "apply_render_visibility" in reader
    assert "local.mesh_visibility_known" in reader
    assert "*render_time + kRenderTimeSlack >= local.mesh_visibility_time" in reader
    assert "kRenderTimeSlack = 0.12F" in reader
    assert "temporal.observed_render_advance = true;" in reader
    assert "return true;" in reader.split("if (!temporal.render_initialized)", 1)[1]
    assert "float mesh_visibility_time{};" in header
    assert "bool mesh_visibility_known{};" in header
    assert "out.mesh_visibility_known = true;" in reader


def test_config_persists_silent_aim_and_removes_hitbox_and_bullet_track():
    config = read(SRC / "config_store.cpp")
    settings = read(INCLUDE / "settings.h")
    assert 'out << "version=32' in config
    assert "RN_BOOL(v3_silent_aim)" in config
    assert '"v3_silent_scale"' in config
    assert "std::clamp(s.v3_silent_scale, 1.0F, 1000000.0F)" in config
    for token in ("hitbox_volume", "HitboxVolume", "bullet_track", "BulletTrack"):
        assert token not in config + settings


def test_bullet_track_has_no_runtime_or_profile_residue():
    combined = "\n".join(
        read(path) for path in (
            ROOT / "jni/Android.mk",
            SRC / "feature_engine.cpp",
            INCLUDE / "feature_engine.h",
            INCLUDE / "settings.h",
            SRC / "config_store.cpp",
            SRC / "ui.cpp",
            INCLUDE / "offset_profile.h",
            SRC / "offset_profile.cpp",
            ROOT / "profiles/CL-56878558.ini",
        )
    )
    for token in ("bullet_track", "BulletTrack", "cached_projectile_shoot_dir", "bullet_target_"):
        assert token not in combined


def test_esp_renderer_and_visibility_paths_remain_untouched():
    feature = read(SRC / "feature_engine.cpp")
    for token in (
        "settings_.boxes",
        "settings_.skeleton",
        "settings_.snapline",
        "settings_.offscreen_warning",
        "settings_.visibility_colors",
        "draw_offscreen_warning",
        "confirmed_visible ? settings_.chams_visible : settings_.chams_hidden",
    ):
        assert token in feature


def test_runtime_write_paths_are_explicit():
    write_users = sorted(
        path.name for path in SRC.glob("*.cpp") if "write_bytes(" in read(path)
    )
    assert write_users == [
        "model_color_runtime.cpp",
        "player_speed_manager.cpp",
        "process_memory.cpp",
        "tracking_manager.cpp",
        "v3_aim_manager.cpp",
        "v3_silent_manager.cpp",
    ]


def test_retired_model_fill_and_masskill_have_no_active_residue():
    combined = "\n".join(
        read(path) for path in (
            ROOT / "jni/Android.mk",
            SRC / "feature_engine.cpp",
            INCLUDE / "feature_engine.h",
            INCLUDE / "game_reader.h",
            SRC / "game_reader.cpp",
            INCLUDE / "settings.h",
            SRC / "config_store.cpp",
            SRC / "ui.cpp",
            INCLUDE / "offset_profile.h",
            SRC / "offset_profile.cpp",
            ROOT / "profiles/CL-56878558.ini",
            ROOT / "profiles/CL-56430492.ini",
        )
    )
    for token in (
        "mass_kill", "character_fill",
        "NativeChams", "ModelChamsManager", "draw_character_fill",
        "mesh_render_custom_depth", "pawn_component_", "fort_game_data_",
    ):
        assert token not in combined
    assert "chams_visible" in combined
    assert "chams_hidden" in combined


def test_player_speed_matches_verified_float_donor_contract_and_has_ui_lifecycle():
    manager = read(SRC / "player_speed_manager.cpp")
    header = read(INCLUDE / "player_speed_manager.h")
    feature = read(SRC / "feature_engine.cpp")
    settings = read(INCLUDE / "settings.h")
    ui = read(SRC / "ui.cpp")
    make = read(JNI / "Android.mk")
    for token in (
        "kLuaSearchValue = 0.0005F",
        "kLuaFloatSignature = 0x3A03126FU",
        "kLuaDefaultValue = 0.021F",
        "kMaxMatches = 500U",
        "lua-float/scanning",
        "lua-float/active",
        "lua-float/not-found",
        "original_bits",
        "applied_bits",
        "restore_catalog",
        "stale_dropped",
        "gameguardian_float_range",
        "patch_limit",
        "request.patch_limit",
        "kRewritePeriod",
        "memory_.read_bytes",
        "memory_.write_bytes",
    ):
        assert token in manager
    assert "player_speed_scale{42.0F}" in settings
    assert "player_speed_patch_limit{500}" in settings
    assert "bool player_speed{false}" in settings
    assert "player_speed_manager.cpp" in make
    assert '"人物加速"' in ui and '"Player Speed"' in ui
    assert '"##player_speed_scale"' in ui
    assert '"##player_speed_custom"' in ui
    assert '"##player_speed_patch_limit"' in ui


def test_player_speed_preserves_donor_ratio_and_restores_on_identity_changes():
    manager = read(SRC / "player_speed_manager.cpp")
    header = read(INCLUDE / "player_speed_manager.h")
    feature = read(SRC / "feature_engine.cpp")
    assert "bits(kLuaSearchValue * scale)" in manager
    assert "diagnostics_.value = kLuaSearchValue * applied_scale_" in manager
    assert "kLuaSearchValue * 42.0F" in manager or "kLuaSearchValue * applied_scale_" in manager
    assert "kLuaDefaultValue * scale" not in manager
    assert "active_pid_ == memory_.pid()" in manager
    assert "active_pid_ = -1; active_world_ = 0" in manager
    assert "if (identity_changed || !active.enabled)" in manager
    assert "request.patch_limit != active.patch_limit" in manager
    assert "if (active_pid_ > 0 || !records_.empty()) restore_catalog();" in manager
    assert "std::clamp(scale, 1.0F, 100.0F)" in manager
    assert "float applied_scale_{42.0F};" in header
    assert "int active_patch_limit_{500};" in header
    assert "const bool active = settings_.player_speed && frame_.pid > 0" in feature
    assert "settings_.player_speed_patch_limit" in feature
    assert "frame_.module_base != 0" in feature
    assert abs(0.0005 * 42.0 - 0.021) < 1e-7


def test_misc_menu_keeps_only_player_speed_after_appearance():
    ui = read(SRC / "ui.cpp")
    start = ui.index("} else if (category_ == 2)")
    misc = ui[start:ui.index("} else {", start)]
    assert misc.index('row(2, tr(language, "') < misc.index('"Appearance"')
    assert misc.index('row(3, tr(language, "') < misc.index('"Player Speed"')
    assert '"MassKill"' not in misc
    assert '"Model & Fill"' not in misc
    assert '"模型与填充"' not in misc


def test_native_tracking_is_independent_sibling_and_settings_persist():
    ui=read(SRC / "ui.cpp")
    assert 'row(3, tr(language, "子弹追踪", "Bullet Tracking")' in ui
    assert 'else if (selected == 3) tracking_detail();' in ui
    aim=ui[ui.index("void Ui::aim_detail()"):ui.index("void Ui::tracking_detail()")]
    assert "tracking_enabled" not in aim
    config=read(SRC / "config_store.cpp")
    for key in ("tracking_enabled","tracking_players","tracking_ai","tracking_prediction","tracking_draw_line","tracking_draw_fov",
                "tracking_angle_deg","tracking_distance_m","tracking_lead_ms","tracking_projectile_mps","tracking_bone"):
        assert key in config and key in read(INCLUDE / "settings.h")
    assert 'bool tracking_enabled{false}' in read(INCLUDE / "settings.h")
    assert 'tracking_.update(frame_, settings_, reader_.profile())' in read(SRC / "feature_engine.cpp")


def test_native_tracking_bounded_io_and_recovery_contract():
    manager=read(SRC / "tracking_manager.cpp")
    assert 'shortlist.size()>5' in manager
    assert 'std::chrono::milliseconds(16)' in manager
    assert 'std::chrono::milliseconds(250)' in manager
    assert 'flock(lock_fd_,LOCK_EX|LOCK_NB)' in manager
    assert 'pread64(fd_' in manager and 'pwrite64(fd_' in manager
    assert 'journal(false)' in manager and 'fsync(fd)' in manager
    assert 'value!=original_' in manager and 'actual!=p::fingerprint' in manager
    assert 'stop PC controller' in manager
    assert 's.aim_enabled' not in manager
    assert 'cached_projectile_shoot_dir' not in manager
    assert 'request_stop' in read(SRC / "main.cpp")
    assert 'features.stop_tracking()' in read(SRC / "main.cpp")
    assert 'tracking_manager.cpp' in read(JNI / "Android.mk")


def test_tracking_generated_payload_matches_binary_and_is_384_bytes():
    import re
    header=read(INCLUDE / "tracking_profile.h")
    match=re.search(r"probe_enc = \{([^}]+)\}",header)
    assert match
    encoded=bytes(int(x,16) for x in re.findall(r"0x[0-9a-f]+",match.group(1)))
    assert len(encoded)==384
    k=0xA7
    plain=bytearray()
    for i,b in enumerate(encoded):
        plain.append(b^k)
        k=(k*73+19+i)&0xFF
    probe=(ROOT / "tools/tracking_probe.bin").read_bytes()
    assert bytes(plain)==probe
    assert "code_rva=0x16AC2920" in header
    assert "probe_xor0=0xA7" in header
    assert probe[:24] not in header.encode("utf-8")
    manager=read(SRC / "tracking_manager.cpp")
    assert "decode_probe()" in manager
    assert "wipe_cave()" in manager
    assert "stage_probe()" in manager
    assert "AGGTRK2!" not in manager
    assert "auto out=p::probe;" not in manager
    assert probe[:24] not in manager.encode("utf-8")


def test_tracking_visibility_persisted_independently_and_filters_before_shortlist():
    config=read(SRC / "config_store.cpp")
    assert "RN_BOOL(tracking_visible_only)" in config
    assert "RN_LOAD_BOOL(tracking_visible_only)" in config
    assert "bool tracking_visible_only{false}" in read(INCLUDE / "settings.h")
    ui=read(SRC / "ui.cpp")
    assert "&settings_.tracking_visible_only" in ui
    manager=read(SRC / "tracking_manager.cpp")
    assert manager.index("visible_allowed(")<manager.index("shortlist.emplace_back")
    assert "e.visibility_known,e.visible" in manager


def test_tracking_stable_identity_selection_replaces_score_switching():
    manager=read(SRC / "tracking_manager.cpp")
    assert "target_lock_.select(" in manager
    assert "sticky->score" not in manager
    assert "if(valid.empty()){last_actor_=0" not in manager
    assert manager.index("if(!disable())")<manager.index("target_lock_.select(")
    selection=read(INCLUDE / "tracking_selection.h")
    assert "targets[i].index==index" in selection
    assert "now-last_valid<.25" in selection


def test_tracking_ui_omits_removed_explanatory_paragraphs():
    ui=read(SRC / "ui.cpp")
    panel=ui[ui.index("void Ui::tracking_detail()"):ui.index("void Ui::silent_aim_detail()")]
    for phrase in ("独立于触控 AIM", "运行不经过 PC", "修正计数不等于命中数", "启用前先停止 PC", "Independent of touch AIM", "Stop the PC controller first"):
        assert phrase not in panel
    for token in ("tracking_visible_only","tracking_angle_deg","tracking_distance_m","tracking_bone","tracking_lead_ms","tracking_draw_line","Calls %u"):
        assert token in panel


def test_all_ui_sliders_use_scoped_step_buttons():
    ui=read(SRC / "ui.cpp")
    assert ui.count("ImGui::SliderFloat(")==1
    assert ui.count("ImGui::SliderInt(")==1
    wrappers=ui[ui.index("bool full_width_slider_float("):ui.index("bool bool_mode_selector(")]
    assert wrappers.count("slider_step_buttons(")==2
    assert wrappers.count("ImGui::PushID(id)")==3
    assert "ImGuiItemFlags_ButtonRepeat" in ui
    assert "BeginDisabled(value<=minimum)" in ui
    assert "BeginDisabled(value>=maximum)" in ui
    assert "###decrement" in ui and "###increment" in ui


def test_percent_slider_preserves_normalized_storage():
    ui=read(SRC / "ui.cpp")
    assert "float percent=*normalized*100.F;" in ui
    assert "if(changed)*normalized=percent*.01F;" in ui
    assert ui.count("full_width_slider_percent(")==5


def test_tracking_ring_style_is_persistent_and_drives_renderer():
    settings=read(INCLUDE / "settings.h");config=read(SRC / "config_store.cpp");feature=read(SRC / "feature_engine.cpp")
    for key in ("tracking_fov_thickness","tracking_fov_color"):
        assert key in settings and key in config and key in feature
    assert "ring_style::width(settings_.tracking_fov_thickness)" in feature
    assert "ring_style::channel(rgba[3],3)" in feature
    assert "radius, IM_COL32(100,235,199,100)" not in feature
    assert "ring_style::channel(as_float" in config


def test_tracking_ring_editor_uses_fine_buttons_and_opacity():
    ui=read(SRC / "ui.cpp")
    assert 'full_width_slider_float("##tracking_fov_thickness"' in ui
    assert 'full_width_slider_percent("##tracking_fov_opacity"' in ui
    assert 'ImGui::ColorEdit4("##tracking_fov_color"' in ui
    assert "&settings_.tracking_fov_color[3]" in ui


def test_silent_hv_does_not_enable_ordinary_aim_or_angle_writer():
    feature=read(SRC / "feature_engine.cpp")
    aim=feature[feature.index("void FeatureEngine::update_aim("):feature.index("void FeatureEngine::update_v3_silent()")]
    assert "if (!settings_.aim_enabled || !frame_.local.valid)" in aim
    assert "settings_.v3_silent_aim" not in aim
    assert "silent_smoothing" not in aim
    assert "v3_silent_.update_angle(" not in feature
    assert "update_angle" not in read(INCLUDE / "v3_silent_manager.h")
    assert "v3_aim_.update(true" in aim
    assert "v3_silent_.update_hv(settings_.v3_silent_aim, settings_.v3_silent_scale" in feature
    source=read(SRC / "v3_silent_manager.cpp")
    hv=source[source.index("void V3SilentManager::update_hv"):source.index("bool V3SilentManager::scan_hv_catalog")]
    assert "requested_.enabled =" not in hv
    assert "requested_.hv_enabled = active" in hv


def test_prediction_line_custom_style_persistence_and_rendering():
    config=read(SRC / "config_store.cpp");settings=read(INCLUDE / "settings.h");feature=read(SRC / "feature_engine.cpp")
    for name in ("tracking_line_thickness","tracking_line_color","tracking_lead_line_thickness","tracking_lead_line_color"):
        assert name in config and name in settings and name in feature
    assert "color,line_width" in feature
    assert "lead_color,lead_width" in feature
    assert "IM_COL32(255,199,95,210)" not in feature
    assert "ring_style::default_line_color" in config and "ring_style::default_lead_color" in config


def test_prediction_line_editors_support_step_buttons_and_opacity():
    ui=read(SRC / "ui.cpp")
    assert 'full_width_line_style("##tracking_line_style"' in ui
    assert 'full_width_line_style("##tracking_lead_line_style"' in ui
    helper=ui[ui.index("bool full_width_line_style("):ui.index("bool bool_mode_selector(")]
    assert "full_width_slider_float(" in helper and "full_width_slider_percent(" in helper
    assert "ImGui::ColorEdit4(" in helper and "ImGui::PushID(id)" in helper
    assert "return changed" in helper


def test_team_filter_default_persistent_and_independent():
    settings=read(INCLUDE / "settings.h");config=read(SRC / "config_store.cpp");ui=read(SRC / "ui.cpp")
    assert "bool tracking_filter_teammates{true}" in settings
    assert "RN_BOOL(tracking_filter_teammates)" in config
    assert "RN_LOAD_BOOL(tracking_filter_teammates)" in config
    assert "&settings_.tracking_filter_teammates" in ui


def test_team_filter_precedes_shortlist_and_sticky_selection():
    src=read(SRC / "tracking_manager.cpp")
    assert src.index("team_allowed(s.tracking_filter_teammates") < src.index("shortlist.emplace_back") < src.index("target_lock_.select")
    assert "f.local.player_state!=0,f.local.team,e.player_state!=0,e.team" in src


def test_color_picker_beside_style_title_before_thickness():
    ui=read(SRC / "ui.cpp")
    helper=ui[ui.index("bool full_width_line_style("):ui.index("bool bool_mode_selector(")]
    assert helper.index("TextUnformatted(label)") < helper.index("ImGui::SameLine()") < helper.index("ColorEdit4") < helper.index("full_width_slider_float")
    ring=ui[ui.index("if(settings_.tracking_draw_fov){"):ui.index('text_wrapped_disabled(tr(language,"十字是实时人物骨骼')]
    assert ring.index("TextUnformatted") < ring.index("SameLine") < ring.index("ColorEdit4") < ring.index("full_width_slider_float")
