param(
    [string]$Variant = "boxonly",
    [string]$BuildId = 'CL59343437-v27-boxonly',
    [string]$BinaryPath = '',
    [string]$FontPath = '',
    [string]$LicensePath = '',
    [string]$ProfilePath = '',
    [string]$SourceArchivePath = '',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $BinaryPath) { $BinaryPath = Join-Path $root "libs-boxonly\arm64-v8a\REBOOT-ESP" }
if (-not $OutputPath) { $OutputPath = Join-Path (Split-Path $root -Parent) "REBOOT-ESP-$BuildId.sh" }

$BinaryPath = (Resolve-Path $BinaryPath).Path

$binaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($BinaryPath))
$requiredFields = @('version=32', 'entity_refresh_hz=', 'performance_preset=', 'recovery_scan_kb=', 'overlay_scale_percent=', 'overlay_refresh_vote=', 'boxes=')
foreach ($banned in @('tracking_enabled=', 'v3_silent_aim=', 'model_chams_enabled=', 'online_name_lookup=', 'player_speed_scale=', 'aim_enabled=', 'aim_touch_output=', 'skeleton=', 'snapline=', 'head_circle=')) {
    if ($binaryText.Contains($banned)) {
        throw "boxonly binary still contains deleted feature '$banned'"
    }
}
foreach ($required in $requiredFields) {
    if (-not $binaryText.Contains($required)) {
        throw "binary contract missing '$required'; rebuild the selected variant before packaging"
    }
}

function Get-GzipBase64([string]$Path) {
    $input = [IO.File]::OpenRead($Path)
    $memory = [IO.MemoryStream]::new()
    try {
        $gzip = [IO.Compression.GZipStream]::new(
            $memory, [IO.Compression.CompressionLevel]::Optimal, $true)
        try { $input.CopyTo($gzip) } finally { $gzip.Dispose() }
        return [Convert]::ToBase64String($memory.ToArray())
    } finally {
        $input.Dispose()
        $memory.Dispose()
    }
}

$binaryB64 = Get-GzipBase64 $BinaryPath
$binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $BinaryPath).Hash.ToLowerInvariant()
$sourceHash = if ($SourceArchivePath) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath (Resolve-Path $SourceArchivePath)).Hash.ToLowerInvariant()
} else { '' }

$script = @"
#!/system/bin/sh
: # set -u disabled for Android sh compatibility
PATH=/sbin:/system/bin:/system/xbin:/vendor/bin:/vendor/xbin:/odm/bin:/product/bin:/su/bin:/data/adb/magisk:/data/adb/ksu/bin:/data/adb/ap/bin:`$PATH
export PATH
find_su() {
  for x in /system/bin/su /system/xbin/su /sbin/su /su/bin/su /data/adb/magisk/su /data/adb/ksu/bin/su /data/adb/ap/bin/su su; do
    if command -v "`$x" >/dev/null 2>&1; then command -v "`$x"; return 0; fi
    if [ -x "`$x" ]; then echo "`$x"; return 0; fi
  done
  return 1
}
run_root() {
  if [ "`$(id -u 2>/dev/null)" = "0" ]; then sh -c "`$1"; return `$?; fi
  _SU_BIN="`$(find_su)" || { echo "su not found/inaccessible; please allow Root for the terminal/MT app" >&2; return 127; }
  "`$_SU_BIN" -c "`$1"
}
umask 077
VARIANT="$Variant"
BUILD_ID="$BuildId"
CONFIG="/data/local/tmp/HX-MOD-v11-boxonly.ini"
TG_CACHE="/data/media/0/Android/data/org.telegram.messenger/cache"
TG_FILE="`$TG_CACHE/-6136477246659695649_97.jpg"
TG_CHANNEL="https://t.me/+Wy5NefYeKvk0MDI1"
BINARY_SHA256=$binaryHash
SOURCE_ARCHIVE_SHA256=$sourceHash
verify_file() {
  actual="`$(sha256sum "`$1" | awk '{print `$1}')"
  [ "`$actual" = "`$2" ] || { echo "hash mismatch: `$1" >&2; return 1; }
}
decode_binary_to() {
  dest="`$1"
  rm -f "`$dest"
  base64 -d <<'REBOOTNOVA_BIN_GZ_B64' | gzip -dc > "`$dest" || return 1
$binaryB64
REBOOTNOVA_BIN_GZ_B64
  verify_file "`$dest" "`$BINARY_SHA256" || { rm -f "`$dest"; return 1; }
  chmod 0755 "`$dest"
}
valid_config() {
  run_root "[ -s '`$1' ] && grep -q '^version=' '`$1' 2>/dev/null && grep -Eq '^(boxes|skeleton|fps_cap|theme)=' '`$1' 2>/dev/null"
}
prepare_config() {
  if valid_config "`$CONFIG"; then
    echo "Using existing config: `$CONFIG"
    return 0
  fi
  for old in "/data/local/tmp/HX-MOD.ini" "/data/local/tmp/REBOOT-ESP.ini" \
             "/data/local/tmp/HX-MOD-v11-official.ini" "/data/local/tmp/HX-MOD-v11-test.ini"; do
    if [ "`$old" = "`$CONFIG" ]; then continue; fi
    if valid_config "`$old"; then
      run_root "cp -f '`$old' '`$CONFIG' && chmod 0600 '`$CONFIG'" || return 1
      run_root "sed -i '/^tracking_/d; /^model_chams_/d; /^v3_silent_/d; /^player_speed/d; /^online_name_lookup=/d; /^skeleton=/d; /^snapline=/d; /^names=/d; /^head=/d; /^aim_/d; /^v3_aim_/d; /^distance=/d' '`$CONFIG'" || return 1
      echo "Migrated previous config: `$old -> `$CONFIG"
      return 0
    fi
  done
  echo "No previous config found; V5 defaults will be used."
}
sanitize_variant_config() {
  run_root "if [ -f '`$CONFIG' ]; then sed -i '/^tracking_/d; /^model_chams_/d; /^v3_silent_/d; /^player_speed/d; /^online_name_lookup=/d; /^skeleton=/d; /^snapline=/d; /^names=/d; /^head=/d; /^aim_/d; /^v3_aim_/d; /^distance=/d; /^boxes=/d; /^max_distance_m=/d' '`$CONFIG'; printf '%s\\n' 'boxes=true' 'max_distance_m=1000' >> '`$CONFIG'; fi" || return 1
}
telegram_channel_marker() {
  run_root "mkdir -p '`$TG_CACHE'" || { echo "Telegram 验证: 缓存目录创建失败" >&2; return 0; }
  sleep 1
  echo "频道`$TG_CHANNEL"
  echo ""
  run_root "printf '%s\\n' 'NEXUS' > '`$TG_FILE'" || { echo "Telegram 验证: 标记写入失败" >&2; return 0; }
  echo "成功"
  return 0
}
launch_from_memory() {
  telegram_channel_marker
  EXTRA="`$*"
  work="`$(mktemp -d /data/local/tmp/.hx-esp-XXXXXX)" || return 1
  decode_binary_to "`$work/REBOOT-ESP" || { rm -rf "`$work"; return 1; }
  run_root "nohup '`$work/REBOOT-ESP' --config '`$CONFIG' `$EXTRA </dev/null >/dev/null 2>&1 &" || { rm -rf "`$work"; return 1; }
  sleep 1
  pid="`$(run_root "pidof REBOOT-ESP" 2>/dev/null | awk '{print `$1}')"
  rm -rf "`$work"
  [ -n "`$pid" ] || return 1
}
stop_running() {
  run_root "pkill -TERM -x REBOOT-ESP >/dev/null 2>&1 || true"
  wait_count=0
  while run_root "pidof REBOOT-ESP >/dev/null 2>&1"; do
    wait_count=`$((wait_count + 1))
    [ "`$wait_count" -lt 15 ] || { echo "Old overlay still running; abort instead of racing hooks" >&2; return 1; }
    sleep 1
  done
}
launch_detached() {
  stop_running || return 1
  prepare_config && sanitize_variant_config || return 1
  launch_from_memory || { echo "REBOOT-ESP exited" >&2; return 1; }
  pid="`$(run_root "pidof REBOOT-ESP" 2>/dev/null | awk '{print `$1}')"
  echo "REBOOT-ESP slim UI started (PID `$pid, config `$CONFIG)."
}
usage() { echo "REBOOT-ESP slim | --verify | --stop | --profile FILE --config FILE --font TTF --lang zh|en --probe"; }
if [ `$# -eq 0 ]; then launch_detached; exit `$?; fi
case "`$1" in
  --help) usage ;;
  --stop) stop_running ;;
  --verify)
    tmp="`$(mktemp /data/local/tmp/.hx-esp-XXXXXX)" || exit 1
    decode_binary_to "`$tmp" && echo "verified binary=`$BINARY_SHA256 source_archive=`$SOURCE_ARCHIVE_SHA256"
    rc=`$?; rm -f "`$tmp"; exit `$rc ;;
  --profile|--config|--font|--lang|--probe) stop_running && prepare_config && sanitize_variant_config && launch_from_memory "`$*"; exit `$? ;;
  *) usage; exit 2 ;;
esac
"@
[IO.File]::WriteAllText($OutputPath, $script.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
Write-Output "SLIM_PACKAGE_WRITTEN=$OutputPath BINARY_SHA256=$binaryHash SOURCE_ARCHIVE_SHA256=$sourceHash"
