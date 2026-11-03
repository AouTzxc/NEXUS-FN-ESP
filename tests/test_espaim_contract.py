#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BANNED = [
    "OnlineNameResolver",
    "TrackingManager",
    "V3SilentManager",
    "ModelColorRuntime",
    "PlayerSpeedManager",
    "SegmentedRecorder",
    "v3_silent_aim",
    "online_name_lookup",
    "model_chams_enabled",
    "tracking_enabled",
    "player_speed_scale",
    "RN_WITH_TRACKING",
]
SCAN_DIRS = [ROOT / "jni" / "src" / "rebootnova", ROOT / "jni" / "include" / "rebootnova", ROOT / "jni"]
ok = True
for folder in SCAN_DIRS:
    for path in folder.rglob("*"):
        if path.suffix.lower() not in {".cpp", ".h", ".inc", ".mk"}:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in BANNED:
            if token in text:
                print(f"FAIL {path.relative_to(ROOT)} contains {token}")
                ok = False
mk = (ROOT / "jni" / "Android.mk").read_text(encoding="utf-8")
for src in ["tracking_manager.cpp", "v3_silent_manager.cpp", "model_color_runtime.cpp",
            "player_speed_manager.cpp", "segmented_recorder.cpp"]:
    if src in mk:
        print(f"FAIL Android.mk still lists {src}")
        ok = False
ui = (ROOT / "jni" / "src" / "rebootnova" / "ui.cpp").read_text(encoding="utf-8")
if "V26" not in ui:
    print("FAIL UI does not advertise V26")
    ok = False
if "静默自瞄" in ui or "子弹追踪" in ui or "人物上色" in ui or "联网昵称" in ui:
    print("FAIL UI still contains deleted feature strings")
    ok = False
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
