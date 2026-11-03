param(
    [string]$NdkPath,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$jni = Join-Path $root 'jni'
if (-not $NdkPath) { $NdkPath = $env:ANDROID_NDK_HOME; if (-not $NdkPath) { $NdkPath = $env:ANDROID_NDK_ROOT } }
if (-not $NdkPath) {
    $sdkCandidates = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android\Sdk')) | Where-Object { $_ }
    foreach ($sdk in $sdkCandidates) {
        $found = Get-ChildItem (Join-Path $sdk 'ndk') -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
        if ($found) { $NdkPath = $found.FullName; break }
    }
}
if (-not $NdkPath) { Write-Warning 'Android NDK not found. Source is ready; BUILD_PENDING_NDK.'; exit 10 }
$ndkBuild = Join-Path $NdkPath 'ndk-build.cmd'
if (-not (Test-Path -LiteralPath $ndkBuild)) { throw "ndk-build.cmd not found under $NdkPath" }
& python (Join-Path $root 'tools\embed_font.py')
if ($LASTEXITCODE -ne 0) { throw "embed_font.py failed with exit code $LASTEXITCODE" }
$variantObj = Join-Path $root 'obj-boxonly'
$variantLibs = Join-Path $root 'libs-boxonly'
if ($Clean) { & $ndkBuild -C $jni NDK_OUT=$variantObj NDK_LIBS_OUT=$variantLibs clean }
& $ndkBuild -C $jni NDK_PROJECT_PATH=$root NDK_APPLICATION_MK=$jni\Application.mk NDK_OUT=$variantObj NDK_LIBS_OUT=$variantLibs
if ($LASTEXITCODE -ne 0) { throw "ndk-build failed with exit code $LASTEXITCODE" }
$bin = Join-Path $variantLibs 'arm64-v8a\REBOOT-ESP'
if (-not (Test-Path -LiteralPath $bin)) { throw "expected ELF missing: $bin" }
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $bin).Hash
Write-Output "BUILD_PASS_ARM64 VARIANT=boxonly ELF_SHA256=$hash"
