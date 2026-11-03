"""Build the read-only Android offset dump ELF."""
import os
import pathlib
import subprocess

R = pathlib.Path(__file__).resolve().parents[1]
ndk = pathlib.Path(os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT") or "")
if not ndk:
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or ""
    ndk_root = pathlib.Path(sdk) / "ndk" if sdk else None
    if ndk_root and ndk_root.is_dir():
        kids = sorted(ndk_root.iterdir(), reverse=True)
        ndk = kids[0] if kids else pathlib.Path()
host = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
clang = ndk / "toolchains/llvm/prebuilt" / host / "bin" / ("clang++.exe" if os.name == "nt" else "clang++")
if not clang.is_file():
    raise SystemExit(f"clang++ not found: {clang}")
out = R / "build/offset-dump"
out.mkdir(parents=True, exist_ok=True)
elf = out / "offset_dump"
cmd = [
    str(clang),
    "--target=aarch64-linux-android28",
    "-std=c++20",
    "-O2",
    "-static-libstdc++",
    "-fno-exceptions",
    "-fno-rtti",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wl,--gc-sections",
    "-Wl,-s",
    "-I",
    str(R / "jni/include"),
    str(R / "tests/offset_dump.cpp"),
    str(R / "jni/src/rebootnova/process_memory.cpp"),
    "-o",
    str(elf),
]
subprocess.run(cmd, check=True)
print(elf, elf.stat().st_size)
