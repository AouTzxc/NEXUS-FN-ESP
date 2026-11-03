from pathlib import Path


ROOT = Path(__file__).parents[1]


def test_source_tree_complete():
    required = [
        ROOT / "jni/src/rebootnova/main.cpp",
        ROOT / "jni/src/rebootnova/process_memory.cpp",
        ROOT / "jni/src/rebootnova/game_reader.cpp",
        ROOT / "jni/src/rebootnova/feature_engine.cpp",
        ROOT / "jni/src/rebootnova/v3_silent_manager.cpp",
        ROOT / "jni/src/rebootnova/segmented_recorder.cpp",
        ROOT / "profiles/CL-56878558.ini",
        ROOT / "assets/fonts/NotoSansSC-UI-Subset.ttf",
        ROOT / "assets/fonts/OFL.txt",
        ROOT / "third_party/surface_imgui/LICENSE",
    ]
    assert all(path.is_file() and path.stat().st_size > 0 for path in required)


def test_package_template_targets_reboot_esp():
    text = (ROOT / "tools/package.ps1").read_text(encoding="utf-8")
    for token in (
        "REBOOTNOVA_SOURCE_B64",
        "umask 077",
        "chmod 0755",
        "install_builtin_profile",
        "install_builtin_font",
        "profiles/CL-56878558.ini",
        "assets/fonts/NotoSansSC-UI-Subset.ttf",
        'mv -f "`$font_new" "`$APP_DIR/NotoSansSC-UI-Subset.ttf"',
        'mv -f "`$profile_new" "`$APP_DIR/CL-56878558.ini"',
        "--exclude='*.sh'",
        "launch_detached()",
        "nohup '`$BIN'",
        "--config '`$APP_DIR/REBOOT-ESP.ini'",
        "pidof REBOOT-ESP",
        "REBOOT-ESP UI started",
        "--font TTF",
        "${sourceRoot}/jni",
    ):
        assert token in text
    for token in ("--username", "--password", "pidof RebootNova", "setenforce 0"):
        assert token not in text


def test_slim_package_gzip_embeds_runtime_assets_without_source_payload():
    text = (ROOT / "tools/package-slim.ps1").read_text(encoding="utf-8")
    for token in (
        "Get-GzipBase64",
        "REBOOTNOVA_BIN_GZ_B64",
        "gzip -dc",
        "SOURCE_ARCHIVE_SHA256",
        "verify_file",
        "launch_from_memory",
        "REBOOT-ESP slim UI started",
        "binary contract missing",
        "version=31",
        "aim_gamepad_device=",
        "aim_ads_region_min_x=",
        "prepare_config",
        "Migrated previous config",
        'CONFIG="/data/local/tmp/HX-MOD-v11-$Variant.ini"',
        "mktemp -d /data/local/tmp/.hx-esp-XXXXXX",
        "decode_binary_to",
        "`$work/REBOOT-ESP",
        "rm -rf",
    ):
        assert token in text
    assert "REBOOTNOVA_SOURCE_B64" not in text
    assert "REBOOTNOVA_FONT_GZ_B64" not in text
    assert "REBOOTNOVA_PROFILE_B64" not in text
    assert "NotoSansSC-UI-Subset.ttf" not in text
    assert "CL-57566230.ini" not in text
    assert "HX-MOD.ini.backup" not in text
    assert "install_runtime" not in text


def test_runtime_does_not_dump_payloads_into_tmp():
    main = (ROOT / "jni/src/rebootnova/main.cpp").read_text(encoding="utf-8")
    reader = (ROOT / "jni/src/rebootnova/game_reader.cpp").read_text(encoding="utf-8")
    log = (ROOT / "jni/src/rebootnova/runtime_log.cpp").read_text(encoding="utf-8")
    assert 'mkdir("/data/local/tmp/reboot-esp"' not in main
    assert "/data/local/tmp/reboot-esp/runtime.log" not in main
    assert "rebootnova::runtime::initialize();" in main
    assert "My_Android_LoadFontMemory" in main
    assert "builtin_57566230()" in main
    assert '"/data/local/tmp/reboot-esp/account-names.tsv"' not in reader
    assert "if (path && path[0])" in log
    assert 'config{"/data/local/tmp/HX-MOD.ini"}' in main


def test_tracking_runtime_dir_created_outside_locked_core():
    feature = (ROOT / "jni/src/rebootnova/feature_engine.cpp").read_text(encoding="utf-8")
    manager = (ROOT / "jni/src/rebootnova/tracking_manager.cpp").read_text(encoding="utf-8")
    main = (ROOT / "jni/src/rebootnova/main.cpp").read_text(encoding="utf-8")
    tick = feature[feature.index("void FeatureEngine::tick("):feature.index("void FeatureEngine::draw(")]
    assert 'mkdir("/data/local/tmp/reboot-esp", 0700)' in tick
    assert "if (settings_.tracking_enabled)" in tick
    assert tick.index("update_aim(screen_width, screen_height);") < tick.index("tracking_.update(")
    assert tick.index("if (settings_.tracking_enabled)") < tick.index('mkdir("/data/local/tmp/reboot-esp", 0700)') < tick.index("tracking_.update(")
    assert 'mkdir("/data/local/tmp/reboot-esp"' not in main
    assert 'kJournal="/data/local/tmp/reboot-esp/tracking-binding.txt"' in manager
    assert 'kLock="/data/local/tmp/reboot-esp/tracking-owner.lock"' in manager
    assert "mkdir(" not in manager


def test_offset_dump_tool_is_readonly_and_writes_cwd_text():
    dump = (ROOT / "tests/offset_dump.cpp").read_text(encoding="utf-8")
    packer = (ROOT / "tools/package-offset-dump.ps1").read_text(encoding="utf-8")
    builder = (ROOT / "tools/build_offset_dump.py").read_text(encoding="utf-8")
    for token in (
        "REBOOT-ESP offset dump",
        "decode_adrp",
        "decrypt_575",
        "mesh_visibility_time",
        "signature_qword",
        "3222016387",
        "Not recovered by this tool",
        "write_bytes",
    ):
        if token == "write_bytes":
            assert token not in dump
        else:
            assert token in dump
    assert "process_memory.cpp" in builder
    assert "offset_dump.cpp" in builder
    assert "reboot-esp-offsets-" in packer
    assert "REBOOTNOVA_DUMP_GZ_B64" in packer
    assert "--out" in packer
