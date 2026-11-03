# 构建与验证 — 单透视（boxonly）

## 1. 环境

仓库验证环境：

```text
Windows 11 + PowerShell 7.6（Windows PowerShell 5.1 见第 8 节）
Android NDK 27.3.13750724 (r27d)
APP_ABI=arm64-v8a
APP_PLATFORM=android-24
APP_STL=c++_static
C++ standard=c++20
Python 3.11+（tools 脚本与测试）
Git Bash（仅用于 Windows 主机上 bash -n；设备端跑 Android /system/bin/sh）
```

`tools/build.ps1` 按这个顺序找 NDK：`-NdkPath` 参数 → `ANDROID_NDK_HOME` → `ANDROID_NDK_ROOT` → `ANDROID_HOME`/`ANDROID_SDK_ROOT`/`%LOCALAPPDATA%\Android\Sdk` 下 `ndk/` 里版本号最大的目录。都找不到时不报错而是 `exit 10` 并打印 `BUILD_PENDING_NDK`。

## 2. 内嵌字体

`jni/Android.mk` 把 `jni/src/rebootnova/embedded_font.cpp` 列进 `LOCAL_SRC_FILES`，这个文件是生成的、不入库：

```powershell
python .\tools\embed_font.py      # assets/fonts/NotoSansSC-UI-Subset.ttf → gzip 符号数组
```

`build.ps1` 每次构建都会先自动跑一遍，正常不用手动执行。要重造子集字体本身：

```powershell
python .\tools\subset_font.py     # 完整 Noto Sans SC → 界面用子集（OFL 1.1）
```

## 3. Clean ARM64 构建

```powershell
.\tools\build.ps1 -Clean
```

先 `ndk-build clean`（`NDK_OUT=obj-boxonly`、`NDK_LIBS_OUT=libs-boxonly`），再构建 `jni/Android.mk`，最后检查产物并打印哈希：

```text
BUILD_PASS_ARM64 VARIANT=boxonly ELF_SHA256=<sha256>
```

变体开关在 `jni/include/rebootnova/build_variant.h`：boxonly 是 `name="boxonly"`、`with_tracking=false`、`aim_last_module=0`，所以只读方框需要的字段、只暴露方框相关配置。

**要求 0 warning / 0 error。** 一个真实教训：`settings.h` 里曾有一条中文写成 `\82`（少一个反斜杠），clang 报 `unknown escape sequence` 且那条中文是坏的——提交前把 warning 清零，别只盯 error。

## 4. 产物

```text
obj-boxonly/                       中间物，不入库
libs-boxonly/arm64-v8a/REBOOT-ESP  目标二进制（约 2.9 MB，含内嵌字体）
```

## 5. 打包成设备端 sh

先打源码归档（`package-slim.ps1` 要把它的哈希写进 sh）：

```powershell
$root = (Resolve-Path .).Path; $leaf = Split-Path $root -Leaf; $out = Split-Path $root -Parent
tar -a -cf "$out\NEXUS-FN-ESP-source.tar.gz" `
  --exclude="$leaf/.git" --exclude="$leaf/obj-boxonly" --exclude="$leaf/libs-boxonly" `
  --exclude="$leaf/jni/src/rebootnova/embedded_font.cpp" `
  --exclude="$leaf/__pycache__" --exclude="$leaf/**/__pycache__" `
  --exclude="$leaf/.pytest_cache" --exclude="$leaf/**/.pytest_cache" `
  --exclude="$leaf/build" $leaf
```

再打包：

```powershell
.\tools\package-slim.ps1 -Variant boxonly -BuildId 'CL59343437-v27-boxonly' `
  -BinaryPath .\libs-boxonly\arm64-v8a\REBOOT-ESP `
  -SourceArchivePath ..\NEXUS-FN-ESP-source.tar.gz `
  -OutputPath ..\REBOOT-ESP-CL59343437-v27-boxonly.sh
```

成功输出 `SLIM_PACKAGE_WRITTEN=<路径> BINARY_SHA256=… SOURCE_ARCHIVE_SHA256=…`。脚本自身必须存成**带 BOM 的 UTF-8**：任何回落到 Windows PowerShell 5.1（ANSI 代码页 gb2312）的路径，没有 BOM 会把脚本里的中文读成乱码并写进产出的 sh。

sh 的硬性契约（脚本自查，缺一不过）：

- 纯 `/system/bin/sh`，LF 行尾、UTF-8 无 BOM、`bash -n` 必须通过。
- 内嵌 base64+gzip 的 payload 解出来必须与 `BINARY_SHA256` 声明一致，且是 ARM64 ELF（`e_machine=183`）。
- 二进制里**不能出现**全功能血统的键：`aim_enabled=`、`aim_touch_output=`、`tracking_enabled=`、`v3_silent_aim=`、`model_chams_enabled=`、`online_name_lookup=`、`player_speed_scale=`、`skeleton=`、`snapline=`、`head_circle=`。
- 二进制里**必须出现**：`version=`、`boxes=`、`max_distance_m=`。
- 保留电报频道验证段 `telegram_channel_marker`（在 `launch_from_memory` 开头调用，`--verify` 不写标记）：创建 `/data/media/0/Android/data/org.telegram.messenger/cache`、写 `-6136477246659695649_97.jpg` 内容 `NEXUS`、打印频道 `https://t.me/+Wy5NefYeKvk0MDI1`。失败只提示，不阻断启动。
- 配置迁移：默认 `CONFIG=/data/local/tmp/HX-MOD-v11-boxonly.ini`，从旧路径搬过来后用 `sed` 清掉本变体不认的键，再补上 `boxes=true`、`max_distance_m=1000`。

## 6. 测试

推荐两条命令：

```powershell
python -m pytest tests/test_world_decode.py -q                                   # 期望 10 passed
python -m pytest tests -q --ignore=tests/test_espaim_contract.py -p no:cacheprovider
```

判读：

- `tests/test_espaim_contract.py` 在**导入时** `sys.exit()`（它扫的是全功能血统的禁用项集合），必须 `--ignore`；否则 pytest 直接 INTERNALERROR，看不出任何有效结论。
- `test_cpp_contract`、`test_package`、`test_v11_contract`、`test_motion_contract`、`test_live_pose_contract`、`test_performance_contract`、`test_publication_lock_contract` 断言的是自瞄 / 追踪 / 模型换色 / 录屏 / 发布锁这些**不在 boxonly 变体里**的东西，在这个仓库上会失败。它们是共享的逆向基础设施，不是本变体的回归基线。
- 判断自己的改动有没有引入回归：跟改动前的失败清单逐条对比（失败集合完全一致才算没动坏）。

```powershell
python -m pytest tests -q --tb=no --ignore=tests/test_espaim_contract.py -p no:cacheprovider 2>&1 `
  | Select-String '^FAILED' | ForEach-Object { $_.Line } | Sort-Object | Set-Content .\pytest-after.txt -Encoding UTF8
Compare-Object (Get-Content .\pytest-before.txt) (Get-Content .\pytest-after.txt)   # 无输出 = 失败集合一致
```

不装 pytest 时可以先用 `python .\tools\run_contract_tests.py`（只跑 `test_cpp_contract`，boxonly 上会失败，属于上面说明的那一类）。

## 7. 真机验证顺序

1. `sh REBOOT-ESP.sh --verify` — 只校验内嵌 ELF 哈希，不启动。
2. 以 root 启动，游戏在前台；确认方框位置、跟随、阵营（只画玩家）、`PID` 与 `Module@base` 出数。
3. 单页菜单：顶栏中/英切换是否立刻生效；拖动顶栏、右下角缩放、最小化成悬浮按钮再点开是否恢复；`功能` 区改方框颜色与开关；`显示信息` 区字段齐全；`配置` 区保存 / 加载 / 恢复默认与路径提示；`退出` 是否干净关层。
4. 本仓库版本额外确认一条：**起来没有 180 秒开屏**，第一帧就是菜单加方框。
5. 换 CL 或偏移存疑时：`sh REBOOT-ESP.sh --probe`，看 logcat tag `REBOOT-ESP` 的 `probe pid=… world=0x… actors=… fp_player=… accepted=… entities=… stage=… error=…` 一行，再决定要不要用 `--profile` 加载外部 ini。

## 8. 已知坑

- **路径带空格**：NDK 在含空格的暂存目录上会失败。构建前先 `Resolve-Path` 确认，或用 `-NdkPath` 指到无空格目录。
- **Windows PowerShell 5.1**：`ndk-build` 往 stderr 写的进度会被 PowerShell 当成错误记录，配合脚本里的 `$ErrorActionPreference='Stop'` 就是 `NativeCommandError` 假失败。用 PowerShell 7+，或者在调用外层先 `$ErrorActionPreference='Continue'` 再合并 `*>&1` 到日志。
- **控制台编码**：Windows 默认代码页可能是 gb2312，Python 脚本打印中文/乱码字符会 `UnicodeEncodeError`。先 `$env:PYTHONIOENCODING='utf-8'`。
- **源码行尾混用**：仓库里既有 CRLF 也有 LF 的文件（例如 `ui.cpp` 是混合的）。批量改代码要按 `\r?\n` 匹配锚点、按原样写回，否则整文件行尾会被改一遍，diff 全是噪音。
- **pytest 缓存**：`-p no:cacheprovider` 避开 `.pytest_cache` 造成的假结果；`__pycache__/` 与 `.pytest_cache/` 已在 `.gitignore` 里。

## 9. 本次开源版本的复现记录

- 相对客户版（单页菜单 R2）的唯一功能差异：删掉开屏。`ui.cpp` 移除 `render_sponsor_splash()`、`SplashCorner`/`splash_corner()`、`add_centered_text()` 与 `Ui::render()` 里的调用；`ui.h` 移除 `sponsor_blocking()`、`render_sponsor_splash()` 与 5 个 `splash_*` 成员；`main.cpp` 里 ESP 绘制不再被 `if (!ui.sponsor_blocking())` 包着，每帧直接画。
- 同时移除内部版本流水文档与 `tools/atg_*.py` 内网调试脚本；`jni/src/rebootnova/embedded_font.cpp` 改由构建生成、不入库。
- 验收命令与结果记录在仓库根的 `SHA256SUMS.txt`（发布资产）与 commit 说明里。
