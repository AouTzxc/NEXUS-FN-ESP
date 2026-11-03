"""Build standalone Android tests (no game access)."""
import os,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
ndk=pathlib.Path(os.environ['ANDROID_NDK_HOME']);host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
clang=ndk/'toolchains/llvm/prebuilt'/host/'bin'/('clang++.exe' if os.name=='nt' else 'clang++')
out=R/'build/v11-probes';out.mkdir(parents=True,exist_ok=True)
for name,flag in [('official',0),('test',1)]:
 cmd=[str(clang),'--target=aarch64-linux-android28','-std=c++20','-O2','-static-libstdc++','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wl,-s',f'-DRN_WITH_TRACKING={flag}','-I',str(R/'jni/include'),str(R/'tests/v11_device_probe.cpp'),str(R/'jni/src/rebootnova/config_store.cpp'),str(R/'jni/src/rebootnova/converter_input.cpp'),'-o',str(out/f'probe-{name}')]
 subprocess.run(cmd,check=True);print(name,(out/f'probe-{name}').stat().st_size)
