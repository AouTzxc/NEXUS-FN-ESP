"""Build standalone uniform color configuration/plan tests; no game contact."""
import pathlib,os,subprocess
r=pathlib.Path(__file__).resolve().parents[1];ndk=pathlib.Path(os.environ['ANDROID_NDK_HOME']);host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
clang=ndk/'toolchains/llvm/prebuilt'/host/'bin'/('clang++.exe' if os.name=='nt' else 'clang++');out=r/'build/model_chams_config';out.mkdir(parents=True,exist_ok=True)
for variant,tracking in [('official',0),('test',1)]:
 subprocess.run([str(clang),'--target=aarch64-linux-android28','-std=c++20','-O2','-Wall','-Wextra','-Werror','-static-libstdc++','-Wl,-s',f'-DRN_WITH_TRACKING={tracking}','-I',str(r/'jni/include'),str(r/'tests/model_chams_config_probe.cpp'),str(r/'jni/src/rebootnova/config_store.cpp'),'-o',str(out/('config-'+variant))],check=True)
 print(out/('config-'+variant))
