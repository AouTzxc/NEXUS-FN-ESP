"""Build bounded, read-only geometry diagnostic; no automatic device contact."""
import os,pathlib,subprocess
r=pathlib.Path(__file__).resolve().parents[1];ndk=pathlib.Path(os.environ['ANDROID_NDK_HOME']);host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
clang=ndk/'toolchains/llvm/prebuilt'/host/'bin'/('clang++.exe' if os.name=='nt' else 'clang++')
out=r/'build/model_color_geometry';out.mkdir(parents=True,exist_ok=True)
subprocess.run([str(clang),'--target=aarch64-linux-android28','-std=c++20','-O2','-Wall','-Wextra','-Werror','-static-libstdc++','-Wl,-s','-I',str(r/'jni/include'),str(r/'tests/model_color_geometry_probe.cpp'),str(r/'jni/src/rebootnova/model_color_discovery.cpp'),'-o',str(out/'geometry-probe')],check=True)
print(out/'geometry-probe')
