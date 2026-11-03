param(
    [string]$BinaryPath = '',
    [string]$OutputPath = '',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutputPath) { $OutputPath = Join-Path (Split-Path $root -Parent) 'REBOOT-ESP.sh' }
if ((Test-Path -LiteralPath $OutputPath) -and -not $Force) { Remove-Item -LiteralPath $OutputPath -Force }
if (-not $BinaryPath) { $candidate = Join-Path $root 'libs\arm64-v8a\REBOOT-ESP'; if (Test-Path -LiteralPath $candidate) { $BinaryPath = $candidate } }
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("rebootnova-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $archive = Join-Path $temp 'source.tar.gz'
    $rootParent = Split-Path $root -Parent
    $rootLeaf = Split-Path $root -Leaf
    tar -czf $archive --exclude="$rootLeaf/obj" --exclude="$rootLeaf/libs" --exclude="$rootLeaf/evidence" --exclude="$rootLeaf/docs" --exclude="$rootLeaf/assets/fonts/NotoSansSC-VF.ttf" --exclude='*.sh' -C $rootParent $rootLeaf
    if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
    $sourceBytes = [IO.File]::ReadAllBytes($archive)
    $sourceB64 = [Convert]::ToBase64String($sourceBytes)
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
    $binaryB64 = ''
    $binaryHash = ''
    if ($BinaryPath) { $binaryBytes = [IO.File]::ReadAllBytes((Resolve-Path $BinaryPath)); $binaryB64 = [Convert]::ToBase64String($binaryBytes); $binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $BinaryPath).Hash.ToLowerInvariant() }
    $sourceRoot = $rootLeaf
    $script = @"
#!/system/bin/sh
set -u
umask 077
APP_DIR="`${TMPDIR:-/data/local/tmp}/reboot-esp"
[ -n "`$APP_DIR" ] || APP_DIR=/data/local/tmp/reboot-esp
BIN="`$APP_DIR/REBOOT-ESP"
SRC="`$APP_DIR/REBOOT-ESP-source.tar.gz"
SOURCE_SHA256=$sourceHash
BINARY_SHA256=$binaryHash
cleanup() { :; }
trap cleanup EXIT INT TERM
usage() { echo "REBOOT-ESP | --verify | --extract-source DIR | --build | --profile FILE --config FILE --font TTF --lang zh|en"; }
decode_source() { mkdir -p "`$APP_DIR"; base64 -d > "`$SRC" <<'REBOOTNOVA_SOURCE_B64'
$sourceB64
REBOOTNOVA_SOURCE_B64
}
install_builtin_profile() {
  stage="`$APP_DIR/profile-stage.`$`$"; rm -rf "`$stage"; mkdir -p "`$stage"
  tar -xzf "`$SRC" -C "`$stage" ${sourceRoot}/profiles/CL-56878558.ini || { rm -rf "`$stage"; echo "profile extraction failed" >&2; return 1; }
  profile_new="`$APP_DIR/CL-56878558.ini.new.`$`$"
  cp "`$stage/${sourceRoot}/profiles/CL-56878558.ini" "`$profile_new"
  chmod 0600 "`$profile_new"; mv -f "`$profile_new" "`$APP_DIR/CL-56878558.ini"
  rm -rf "`$stage"
}
install_builtin_font() {
  stage="`$APP_DIR/font-stage.`$`$"; rm -rf "`$stage"; mkdir -p "`$stage"
  tar -xzf "`$SRC" -C "`$stage" ${sourceRoot}/assets/fonts/NotoSansSC-UI-Subset.ttf ${sourceRoot}/assets/fonts/OFL.txt || { rm -rf "`$stage"; echo "font extraction failed" >&2; return 1; }
  font_new="`$APP_DIR/NotoSansSC-UI-Subset.ttf.new.`$`$"
  cp "`$stage/${sourceRoot}/assets/fonts/NotoSansSC-UI-Subset.ttf" "`$font_new"
  chmod 0600 "`$font_new"; mv -f "`$font_new" "`$APP_DIR/NotoSansSC-UI-Subset.ttf"
  cp "`$stage/${sourceRoot}/assets/fonts/OFL.txt" "`$APP_DIR/NotoSansSC-OFL.txt"
  chmod 0600 "`$APP_DIR/NotoSansSC-OFL.txt"
  rm -rf "`$stage"
}
decode_binary() {
  [ -n "`$BINARY_SHA256" ] || { echo "binary not embedded; use --build" >&2; return 10; }
  mkdir -p "`$APP_DIR"; tmp_bin="`$BIN.new.`$`$"; rm -f "`$tmp_bin"
  base64 -d > "`$tmp_bin" <<'REBOOTNOVA_BINARY_B64'
$binaryB64
REBOOTNOVA_BINARY_B64
  chmod 0755 "`$tmp_bin"; mv -f "`$tmp_bin" "`$BIN"
}
launch_detached() {
  RUN_LOG="`$APP_DIR/launcher.log"
  su -c "pkill -x REBOOT-ESP >/dev/null 2>&1 || true"
  sleep 1
  su -c "nohup '`$BIN' --profile '`$APP_DIR/CL-56878558.ini' --config '`$APP_DIR/REBOOT-ESP.ini' --font '`$APP_DIR/NotoSansSC-UI-Subset.ttf' >'`$RUN_LOG' 2>&1 </dev/null &" || {
    echo "RebootNova launch failed" >&2
    return 1
  }
  sleep 1
  pid="`$(su -c "pidof REBOOT-ESP" 2>/dev/null | awk '{print `$1}')"
  [ -n "`$pid" ] || {
    echo "RebootNova exited before UI startup; log: `$RUN_LOG" >&2
    return 1
  }
  echo "REBOOT-ESP UI started (PID `$pid)."
  printf '\n'
  printf '%s' 'VUkg5bey5ZCv5Yqo77yM5oyJ5Zue6L2m6YCA5Ye6IE1UIOe7iOerr++8iFVJIOWwhue7p+e7rei/kOihjO+8iS4uLg==' | base64 -d
  IFS= read -r _ || :
  echo
}
verify() {
  command -v sha256sum >/dev/null 2>&1 || { echo "sha256sum required" >&2; return 2; }
  decode_source; actual=`$(sha256sum "`$SRC" | awk '{print `$1}'); [ "`$actual" = "`$SOURCE_SHA256" ] || { echo "source hash mismatch" >&2; return 1; }
  if [ -n "`$BINARY_SHA256" ]; then decode_binary; actual=`$(sha256sum "`$BIN" | awk '{print `$1}'); [ "`$actual" = "`$BINARY_SHA256" ] || { echo "binary hash mismatch" >&2; return 1; }; fi
  echo "verified source_sha256=`$SOURCE_SHA256"
}
extract_source() { target="`$1"; [ -n "`$target" ] || { echo "extract target required" >&2; return 2; }; decode_source; mkdir -p "`$target"; tar -xzf "`$SRC" -C "`$target"; echo "source extracted to `$target"; }
if [ `$# -eq 0 ]; then
  if [ -n "`$BINARY_SHA256" ]; then
    decode_source && install_builtin_profile && install_builtin_font && decode_binary && launch_detached
    exit `$?
  fi
  echo "source-only package: build with --build after installing Android NDK"; exit 10
fi
case "`$1" in
  --help) usage ;;
  --verify) verify ;;
  --extract-source) shift; extract_source "`${1:-}" ;;
  --build) decode_source; mkdir -p "`$APP_DIR/build"; tar -xzf "`$SRC" -C "`$APP_DIR/build"; exec ndk-build -C "`$APP_DIR/build/${sourceRoot}/jni" ;;
  --profile|--config|--font|--lang|--probe) decode_source; install_builtin_profile; install_builtin_font; decode_binary; exec su -c "'`$BIN' --profile '`$APP_DIR/CL-56878558.ini' --config '`$APP_DIR/REBOOT-ESP.ini' --font '`$APP_DIR/NotoSansSC-UI-Subset.ttf' `$*" ;;
  *) usage; exit 2 ;;
esac
"@
    [IO.File]::WriteAllText($OutputPath, $script, [Text.UTF8Encoding]::new($false))
    Write-Output "PACKAGE_WRITTEN=$OutputPath SOURCE_SHA256=$sourceHash BINARY_SHA256=$binaryHash"
} finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force } }
