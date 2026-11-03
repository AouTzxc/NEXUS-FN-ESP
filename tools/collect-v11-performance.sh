#!/system/bin/sh
# Read-only diagnostic capture. Enable the performance HUD and reproduce for ~60 s first.
umask 077
OUT="/sdcard/Download/REBOOT-V11-perf-$(date +%Y%m%d-%H%M%S).txt"
{
  echo 'REBOOT V11 diagnostics (no game memory access)'
  date
  printf 'model='; getprop ro.product.model
  printf 'soc='; getprop ro.soc.model
  printf 'android='; getprop ro.build.version.release
  printf 'sdk='; getprop ro.build.version.sdk
  wm size
  echo '--- display snapshot (2 s limit) ---'
  /system/bin/timeout 2 /system/bin/dumpsys display 2>/dev/null | grep -E 'mActiveModeId|mDesiredDisplayModeSpecs|renderFrameRate|fps=|refreshRate=' | head -n 80
  echo '--- recent overlay performance ---'
  logcat -d -s RebootNova:I 2>/dev/null | grep 'PERF ' | tail -n 24
  echo '--- end ---'
} > "$OUT" 2>&1
if [ -s "$OUT" ]; then echo "$OUT"; else echo 'Cannot write report' >&2; exit 1; fi
