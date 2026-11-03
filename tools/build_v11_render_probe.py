"""Build a standalone visible EGL scaling probe; never opens the game process."""
import pathlib,os,subprocess
R=pathlib.Path(__file__).resolve().parents[1];S=R/'third_party/surface_imgui/jni'
ndk=pathlib.Path(os.environ['ANDROID_NDK_HOME']);host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
clang=ndk/'toolchains/llvm/prebuilt'/host/'bin'/('clang++.exe' if os.name=='nt' else 'clang++')
out=R/'build/v11-probes';out.mkdir(parents=True,exist_ok=True)
includes=[R/'jni/include']+[S/'include'/p for p in ['', 'Android_Graphics','Android_my_imgui','Android_touch','ImGui','ImGui/backends','My_Utils']]
sources=[R/'tests/v11_render_probe.cpp']+[S/'src'/p for p in ['Android_Graphics/OpenGLGraphics.cpp','Android_my_imgui/AndroidImgui.cpp','Android_my_imgui/my_imgui_impl_android.cpp','ImGui/imgui.cpp','ImGui/imgui_draw.cpp','ImGui/imgui_tables.cpp','ImGui/imgui_widgets.cpp','ImGui/backends/imgui_impl_opengl3.cpp','My_Utils/stb_image.cpp']]
cmd=[str(clang),'--target=aarch64-linux-android28','-std=c++20','-O2','-static-libstdc++','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wl,-s','-DIMGUI_DISABLE_DEBUG_TOOLS','-DSURFACE_LOG_TAG="RN-V11-PROBE"','-DSURFACE_LOG_LEVEL_ERROR=1','-DSURFACE_LOG_LEVEL_WARN=2','-DSURFACE_LOG_LEVEL_INFO=3','-DSURFACE_LOG_LEVEL_DEBUG=4','-DSURFACE_LOG_LEVEL=2']
for p in includes:cmd+=['-I',str(p)]
cmd += [str(p) for p in sources]+['-llog','-landroid','-ldl','-lEGL','-lGLESv3','-o',str(out/'render-probe')]
subprocess.run(cmd,check=True);print(out/'render-probe',(out/'render-probe').stat().st_size)
