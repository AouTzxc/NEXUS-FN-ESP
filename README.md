# NEXUS-FN-ESP — 单透视（boxonly）ESP

Android arm64 的**外部进程** ESP，只画玩家方框。不注入游戏、不改游戏内存布局，靠 root 直接读 `/proc/PID/mem`。

- 基线：**Fortnite 42.40.0 / CL-59343437**（UE4，`com.epicgames.fortnite`，模块 `libUnreal.so`）。
- 变体：`boxonly`（`jni/include/rebootnova/build_variant.h` 里 `with_tracking = false`、`name = "boxonly"`）。
- 界面：**单页菜单**——顶栏直接是中/英文切换和最小化，页面依次是 功能 → 显示信息 → 配置 → 退出。
- **本仓库的开源版去掉了 180 秒全屏开屏赞助名单**，起来就是菜单和方框。客户发布版仍带 180 秒开屏，那是另一条打包线。
- 中文字体内嵌在二进制里，不依赖外部字体；`--font` 可指定外部 TTF 覆盖。

## 工作方式

| 环节 | 实现 |
| --- | --- |
| 找进程 | 扫 `/proc/*/cmdline` 匹配包名，再用 `/proc/PID/maps` 定位 `libUnreal.so` 基址 |
| 读内存 | `/proc/PID/mem` 加 `process_vm_readv`（arm64 syscall 270），写用 `process_vm_writev`（271） |
| 悬浮层 | `dlopen("libgui.so")` 直接向 SurfaceFlinger 申请图层，OpenGL ES 绘制。不用 Activity、不用无障碍、不用注入 |
| 触控 | 读 `/dev/input` 的 evdev 事件，写 `/dev/uinput` 注入。触控初始化失败不致命，其他环节失败会退出 |
| 玩家筛选 | `UWorld → Levels → Actor` 链 + `actor_class_fingerprint` 指纹识别玩家，只画玩家不画机器人；受 `max_distance_m` 限制 |

需要 root 的三件事：读 `/proc/PID/mem`、创建 SurfaceFlinger 图层、访问 `/dev/input` 与 `/dev/uinput`。
虚拟机 / 容器 / 定制 ROM 只要能给出 root 和这些节点就能跑，代码本身不挑 root 的来源（`su`、`magisk`、`adb shell` 都行）。

## 功能范围

**有：**

- 玩家方框开关、方框颜色；最大距离、FPS 上限、悬浮层缩放、主题、强调色、中/英文、录屏可见性。
- 显示信息：渲染器、FPS、触控状态、PID、`Module@base`、游戏状态、实体数、Build / CL。
- 单页窗口可拖动（顶栏）、可缩放（右下角）、可最小化成悬浮按钮，点开恢复原位原尺寸。
- 配置保存 / 加载 / 恢复默认。

**没有（这些在全功能血统的 [REBOOT-ESP-009](https://github.com/AouTzxc/REBOOT-ESP-009) 里）：** 自瞄、子弹追踪、骨骼、连线、头部圆圈、距离数字、载具标记、世界物品、V3 静默写入、人物填充、分段录屏。

## 目录

```text
jni/Android.mk                     boxonly 编译的文件清单（只编 ESP 需要的）
jni/Application.mk                 arm64-v8a / android-24 / c++_static / c++20
jni/include/rebootnova/            设置、偏移、内存、UI、变体开关等头文件
jni/src/rebootnova/                核心源码（main / game_reader / offset_profile / ui …）
third_party/surface_imgui/         AndroidSurfaceImgui-Enhanced + Dear ImGui（MIT）
profiles/                          CL 偏移配置，含 [V]/[H]/[T] 溯源注释
assets/fonts/                      Noto Sans SC 子集 + OFL 授权
tests/                             源码契约测试与内存解码测试
tools/build.ps1                    clean ARM64 构建（自动跑 embed_font.py）
tools/embed_font.py                字体 → jni/src/rebootnova/embedded_font.cpp
tools/subset_font.py               完整字体 → 子集字体
tools/package-slim.ps1             gzip+base64 内嵌 ELF 的 sh 打包脚本
tools/dump.sh                      设备端偏移 dump 脚本
docs/BUILD.md                      构建、打包、测试、验证细节
AGENTS.md                          贡献者约定（偏移溯源、打包必带项）
```

`jni/Android.mk` 只编译单透视需要的源文件。`native/model_chams/`、部分 `tools/build_model_*.py`、`tools/build_v11_*.py`、以及多数 `tests/test_*_contract.py` 是从全功能血统带过来的共享文件，本变体不编译也不加载，留着是为了共用同一套逆向基础设施——见下文"测试"一节怎么跑。其中 `tools/build_model_chams_payload.py` 之类还需要全功能树的 `model_chams_abi.h`，在本仓库里跑不起来，只当留档。

## 构建

要求：Windows PowerShell（建议 PowerShell 7+）、Android NDK（仓库验证用的版本是 **27.3.13750724 / r27d**）、Python 3。

```powershell
.\tools\build.ps1 -Clean
.\tools\build.ps1 -NdkPath 'C:\Users\<you>\AppData\Local\Android\Sdk\ndk\27.3.13750724' -Clean
```

`build.ps1` 会先跑 `tools/embed_font.py` 生成 `jni/src/rebootnova/embedded_font.cpp`（该文件不入库），再 `ndk-build clean` + 构建，产物固定检查：

```text
libs-boxonly/arm64-v8a/REBOOT-ESP        输出 BUILD_PASS_ARM64 VARIANT=boxonly ELF_SHA256=…
```

仓库验收标准是构建 **0 warning / 0 error**。两个坑：暂存目录不能带空格（NDK 在带空格的路径上会失败）；Windows PowerShell 5.1 会把 `ndk-build` 的 stderr 当错误抛出（`$ErrorActionPreference='Stop'` 时更明显），换 PowerShell 7 或在脚本外层先设 `Continue`。

## 打包成设备端一键 sh

```powershell
tar -a -cf ..\NEXUS-FN-ESP-source.tar.gz --exclude=NEXUS-FN-ESP/.git --exclude=NEXUS-FN-ESP/obj-boxonly `
  --exclude=NEXUS-FN-ESP/libs-boxonly --exclude=NEXUS-FN-ESP/jni/src/rebootnova/embedded_font.cpp NEXUS-FN-ESP

.\tools\package-slim.ps1 -Variant boxonly -BuildId 'CL59343437-v27-boxonly' `
  -BinaryPath .\libs-boxonly\arm64-v8a\REBOOT-ESP `
  -SourceArchivePath ..\NEXUS-FN-ESP-source.tar.gz -OutputPath ..\REBOOT-ESP-CL59343437-v27-boxonly.sh
```

sh 的要求（`package-slim.ps1` 会自查，见 `docs/BUILD.md`）：`/system/bin/sh` 兼容、LF、UTF-8 无 BOM、`bash -n` 通过、内嵌 payload gunzip 后与 `BINARY_SHA256` 一致、禁带全功能血统的配置键、必带 `version=32` / `boxes=` / `max_distance_m=`。

每个单透视包都内置电报频道验证段（`tools/package-slim.ps1` 里的 `telegram_channel_marker`，在 `launch_from_memory` 开头调用）：root 下创建 `/data/media/0/Android/data/org.telegram.messenger/cache`，向 `-6136477246659695649_97.jpg` 写 `NEXUS`，并打印频道 `https://t.me/+Wy5NefYeKvk0MDI1`。失败只提示、不阻断启动，`--verify` 不写标记。这是作者自己的渠道， fork 想改就改模板。

## 设备端运行

```sh
su -c 'sh /data/local/tmp/REBOOT-ESP.sh'         # 启动
sh REBOOT-ESP.sh --verify                        # 只校验内嵌 ELF 哈希，不启动
sh REBOOT-ESP.sh --stop                          # 停止
sh REBOOT-ESP.sh --profile FILE --config FILE --font TTF --lang zh|en --probe
```

`--probe` 走一轮识别后退出并把诊断打到 logcat（tag `REBOOT-ESP`），适合换 CL 后先验证偏移。

## 配置

默认路径 `/data/local/tmp/HX-MOD-v11-boxonly.ini`。旧路径（`HX-MOD.ini`、`REBOOT-ESP.ini`、`HX-MOD-v11-official.ini`、`HX-MOD-v11-test.ini`）会自动迁移，本变体不认识的键会被清掉。界面里只有"保存 / 加载 / 恢复默认 + 路径 + 状态"，其余键改文件即可：

| 键 | 范围 | 说明 |
| --- | --- | --- |
| `version` | 固定 32 | 配置格式版本 |
| `language` | 0 中文 / 1 英文 | 界面语言，顶栏按钮直接切 |
| `boxes` | true / false | 玩家方框 |
| `accent0..3` | 0.0–1.0 | 强调色 RGBA，也是方框颜色 |
| `max_distance_m` | 25–1000 | 只画这个距离内的玩家 |
| `fps_cap` | 20–144 | 悬浮层帧率上限 |
| `overlay_scale_percent` | 50–100 | 悬浮层缩放 |
| `overlay_refresh_vote` | true / false | 刷新率投票 |
| `theme` | 0 暗 / 1 亮 / 2 classic | 主题 |
| `record_visible` | true / false | 悬浮层是否进录屏（改动会重建图层） |
| `performance_preset` | 0–3 | 性能预设 |
| `entity_refresh_hz` | 20–120 | 实体扫描频率 |
| `recovery_scan_kb` | 64–2048 | 识别失败后的恢复扫描窗口 |

## 偏移与溯源

`profiles/CL-59343437.ini` 是当前基线（编译期内置的是 `OffsetProfile::builtin_59343437()`，`--profile` 可加载外部 ini 覆盖）。字段都带注释说明来源：`[V]` 已运行验证、`[H]` 高置信布局、`[T]` 待真机。这个 CL 的 `GWorld` 是加密的，ini 里记了 codec：`bswap64(encoded ^ 0x1296A4C) - 0xF7FED09E`。静态 IDA 候选在真机验证前不算最终偏移。

`profiles/` 里另外四个 ini（CL-56430492、CL-56878558、CL-57316517、CL-57566230）是历史 CL，供参考和旧版本游戏使用。

## 测试

```powershell
python -m pytest tests/test_world_decode.py -q                        # 单透视相关：10 passed
python -m pytest tests -q --ignore=tests/test_espaim_contract.py -p no:cacheprovider
```

- `tests/test_espaim_contract.py` 在导入阶段就 `sys.exit()`（它扫的是全功能血统的禁用项），必须 `--ignore`，否则 pytest 直接 INTERNALERROR。
- `test_cpp_contract / test_package / test_v11_contract / test_motion_contract / test_live_pose_contract / test_performance_contract / test_publication_lock_contract` 断言的是全功能血统的功能（自瞄、追踪、模型换色、录屏、发布锁），在 boxonly 源码上会失败，不是本仓库的回归。仓库的验收口径是：`test_world_decode` 全绿 + clean 构建 0 warning + 打包自查全过 + 真机验证清单。

## 当前验证状态

| 项 | 状态 |
| --- | --- |
| ARM64 clean 构建 0 warning / 0 error | [V] |
| sh 语法（`bash -n`）、`--help`、无 BOM / 无 CRLF、内嵌 payload 哈希一致 | [V] |
| 真机：方框位置与跟随、PID / 模块基址识别 | [V] |
| 真机：单页菜单、中/英切换、最小化与恢复、拖动、缩放、退出 | [V] |
| 真机：去掉开屏后首帧直接进菜单与方框 | [T] 本次改动后待复测 |
| 换 CL 后仅靠 ini 更新（需 `--probe` 实跑） | [T] |

## 许可

源码按 **GPL-3.0** 发布，见 [LICENSE](LICENSE)。第三方按上游许可保留，见 [NOTICE](NOTICE)：`third_party/surface_imgui`（含 Dear ImGui、imstb、stb_image）为 MIT，`assets/fonts/NotoSansSC-UI-Subset.ttf` 为 SIL OFL 1.1，ImGui 里可选的 FreeType 加载器（本工程未编译）为 FTL / GPL-2.0 双许可。

作者保留对自有代码另行授权的权利（dual licensing）：本仓库这份是 GPL-3.0，面向客户的发布包可能另带限制。

## 免责

非官方，与 Epic Games 无关联也无背书；用于逆向学习与内存读取实现参考。用在在线对局有封号风险，自负。不提供任何担保。

## English summary

External-process Android ARM64 ESP for Fortnite 42.40.0 / CL-59343437, boxes only. Memory is read through `/proc/PID/mem` and `process_vm_readv`; the overlay is a SurfaceFlinger layer obtained via `dlopen("libgui.so")`; touch comes from evdev plus `/dev/uinput`. Root is required for all three. The single-page menu shows the language switch and minimize button in the header bar, then Features, Display Info, Configuration and Exit. This open-source build drops the 180-second sponsor splash that the customer package still carries. Source is GPL-3.0 with MIT/OFL third-party components; build with `tools/build.ps1 -Clean` (NDK r27d) and package with `tools/package-slim.ps1`.
