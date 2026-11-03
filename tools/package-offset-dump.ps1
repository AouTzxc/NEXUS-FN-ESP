param(
    [string]$BinaryPath = '',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $BinaryPath) { $BinaryPath = Join-Path $root 'build\offset-dump\offset_dump' }
if (-not $OutputPath) { $OutputPath = Join-Path $root 'tools\dump.sh' }
if (-not (Test-Path -LiteralPath $BinaryPath)) {
    & python (Join-Path $root 'tools\build_offset_dump.py')
    if ($LASTEXITCODE -ne 0) { throw "build_offset_dump.py failed with exit code $LASTEXITCODE" }
}
$BinaryPath = (Resolve-Path $BinaryPath).Path
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
$script = @"
#!/system/bin/sh
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
BINARY_SHA256=$binaryHash
CWD="`$(pwd)"
STAMP="`$(date +%Y%m%d-%H%M%S 2>/dev/null || echo now)"
OUT="`$CWD/reboot-esp-offsets-`$STAMP.txt"
decode_binary_to() {
  dest="`$1"
  rm -f "`$dest"
  base64 -d <<'REBOOTNOVA_DUMP_GZ_B64' | gzip -dc > "`$dest" || return 1
$binaryB64
REBOOTNOVA_DUMP_GZ_B64
  actual="`$(sha256sum "`$dest" | awk '{print `$1}')"
  [ "`$actual" = "`$BINARY_SHA256" ] || { echo "hash mismatch: `$dest" >&2; rm -f "`$dest"; return 1; }
  chmod 0755 "`$dest"
}
work="`$(mktemp -d /data/local/tmp/.hx-dump-XXXXXX)" || exit 1
decode_binary_to "`$work/offset_dump" || { rm -rf "`$work"; exit 1; }
TMPOUT="`$work/reboot-esp-offsets.txt"
run_root "'`$work/offset_dump' --out '`$TMPOUT'"
rc=`$?
if [ -s "`$TMPOUT" ]; then
  cp -f "`$TMPOUT" "`$OUT" 2>/dev/null || run_root "cp -f '`$TMPOUT' '`$OUT'"
fi
rm -rf "`$work"
if [ ! -s "`$OUT" ]; then
  echo "dump failed rc=`$rc (enter a match first; lobby often has no UWorld chain)" >&2
  exit 1
fi
echo "`$OUT"
exit `$rc
"@
[IO.File]::WriteAllText($OutputPath, $script.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
Write-Output "OFFSET_DUMP_SH=$OutputPath BINARY_SHA256=$binaryHash"
