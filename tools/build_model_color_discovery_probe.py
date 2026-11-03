"""Build read-only discovery probe; no device contact and no game write channel linked."""
import pathlib,os,subprocess,sys
R=pathlib.Path(__file__).resolve().parents[1];ndk=pathlib.Path(os.environ['ANDROID_NDK_HOME']);host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
clang=ndk/'toolchains/llvm/prebuilt'/host/'bin'/('clang++.exe' if os.name=='nt' else 'clang++');out=R/'build/model_color_discovery';out.mkdir(parents=True,exist_ok=True)
unit='--unit' in sys.argv
source='model_color_discovery_unit.cpp' if unit else 'model_color_discovery_probe.cpp'
name='discovery-unit' if unit else 'discovery-probe'
subprocess.run([str(clang),'--target=aarch64-linux-android28','-std=c++20','-O2','-Wall','-Wextra','-Werror','-static-libstdc++','-Wl,-s','-I',str(R/'jni/include'),str(R/'tests'/source),str(R/'jni/src/rebootnova/model_color_discovery.cpp'),'-o',str(out/name)],check=True)
print(out/name)
