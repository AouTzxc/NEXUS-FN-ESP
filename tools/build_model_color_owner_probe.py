"""Build standalone owner/channel tests; does not access a game process."""
import os,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];ndk=pathlib.Path(os.environ['ANDROID_NDK_HOME']);host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
clang=ndk/'toolchains/llvm/prebuilt'/host/'bin'/('clang++.exe' if os.name=='nt' else 'clang++');out=R/'build/model_color_owner';out.mkdir(parents=True,exist_ok=True)
cmd=[str(clang),'--target=aarch64-linux-android28','-std=c++20','-O2','-Wall','-Wextra','-Werror','-static-libstdc++','-Wl,-s','-I',str(R/'jni/include')]
cmd+=[str(R/p) for p in ['tests/model_color_owner_probe.cpp','jni/src/rebootnova/model_color_owner.cpp','jni/src/rebootnova/model_color_channel.cpp']]
cmd+=['-o',str(out/'owner-probe')];subprocess.run(cmd,check=True);print(out/'owner-probe')
