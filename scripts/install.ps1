# 安装脚本 — 将插件安装到 OBS Studio
param(
    [string]$BuildType = "RelWithDebInfo",
    [string]$ObsDir = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

# 检测 OBS 安装目录
if (-not $ObsDir) {
    $ObsDir = "${Env:ProgramFiles}\obs-studio"
    if (-not (Test-Path $ObsDir)) {
        $ObsDir = "${Env:ProgramFiles(x86)}\obs-studio"
    }
}

if (-not (Test-Path $ObsDir)) {
    Write-Host "[ERROR] OBS Studio not found. Use -ObsDir to specify path." -ForegroundColor Red
    exit 1
}

Write-Host "OBS 安装目录: $ObsDir" -ForegroundColor Gray

# ---- 复制插件 DLL ----
$buildDir = Join-Path $projectRoot "build"
$dllSrc = Join-Path $buildDir $BuildType "obs-multicam-review.dll"
$dllDst = Join-Path $ObsDir "obs-plugins\64bit\obs-multicam-review.dll"

if (Test-Path $dllSrc) {
    New-Item -ItemType Directory -Force -Path (Split-Path $dllDst) | Out-Null
    Copy-Item $dllSrc $dllDst -Force
    Write-Host "[OK] 插件 DLL → obs-plugins/64bit/" -ForegroundColor Green
} else {
    Write-Host "[WARN] DLL not found at $dllSrc, build first: .\scripts\build.ps1" -ForegroundColor Yellow
}

# ---- 复制 Web UI (内嵌的本地文件版本) ----
$uiDistDir = Join-Path $projectRoot "control-ui\dist"
$uiDstDir = Join-Path $ObsDir "data\obs-plugins\obs-multicam-review\web"

if (Test-Path $uiDistDir) {
    New-Item -ItemType Directory -Force -Path $uiDstDir | Out-Null
    Copy-Item "$uiDistDir\*" $uiDstDir -Recurse -Force
    Write-Host "[OK] Web UI → data/obs-plugins/obs-multicam-review/web/" -ForegroundColor Green
}

# ---- 复制叠加层 ----
$overlaySrc = Join-Path $projectRoot "overlay"
$overlayDst = Join-Path $ObsDir "data\obs-plugins\obs-multicam-review\overlay"

New-Item -ItemType Directory -Force -Path $overlayDst | Out-Null
Copy-Item "$overlaySrc\*" $overlayDst -Recurse -Force
Write-Host "[OK] Overlay → data/obs-plugins/obs-multicam-review/overlay/" -ForegroundColor Green

# ---- 复制 locale (如果有) ----
$localeSrc = Join-Path $projectRoot "plugin\data\locale"
$localeDst = Join-Path $ObsDir "data\obs-plugins\obs-multicam-review\locale"
if (Test-Path $localeSrc) {
    New-Item -ItemType Directory -Force -Path $localeDst | Out-Null
    Copy-Item "$localeSrc\*" $localeDst -Recurse -Force
    Write-Host "[OK] Locale files installed" -ForegroundColor Green
}

Write-Host "`n安装完成! 重启 OBS Studio 后插件生效。" -ForegroundColor Green
Write-Host "`n控制台: http://localhost:9527" -ForegroundColor White
Write-Host "叠加层: http://localhost:9527/overlay/product-card.html" -ForegroundColor White
