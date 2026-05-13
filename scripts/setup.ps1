# Phase 0: 开发环境搭建脚本
# 以管理员身份运行 PowerShell, 执行: .\scripts\setup.ps1

$ErrorActionPreference = "Stop"
Write-Host "============================================" -ForegroundColor Cyan
Write-Host " obs-multicam-review 环境搭建" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# ---- 1. 检查必备工具 ----
Write-Host "`n[1/4] 检查开发工具..." -ForegroundColor Yellow

$missing = @()

# CMake
try {
    $cmakeVer = cmake --version | Select-Object -First 1
    Write-Host "  [OK] CMake: $cmakeVer" -ForegroundColor Green
} catch {
    $missing += "CMake (https://cmake.org/download/)"
    Write-Host "  [MISS] CMake not found" -ForegroundColor Red
}

# MSVC / Visual Studio
$vsPath = "${Env:ProgramFiles}\Microsoft Visual Studio\2022"
if (Test-Path $vsPath) {
    Write-Host "  [OK] Visual Studio 2022" -ForegroundColor Green
} else {
    $vsPath = "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\2022"
    if (Test-Path $vsPath) {
        Write-Host "  [OK] Visual Studio 2022" -ForegroundColor Green
    } else {
        $missing += "Visual Studio 2022 Community (https://visualstudio.microsoft.com/)"
        Write-Host "  [MISS] Visual Studio 2022 not found" -ForegroundColor Red
    }
}

# Node.js
try {
    $nodeVer = node --version
    Write-Host "  [OK] Node.js: $nodeVer" -ForegroundColor Green
} catch {
    $missing += "Node.js 18+ (https://nodejs.org/)"
    Write-Host "  [MISS] Node.js not found" -ForegroundColor Red
}

# OBS Studio
$obsPath = "${Env:ProgramFiles}\obs-studio"
if (Test-Path $obsPath) {
    Write-Host "  [OK] OBS Studio: $obsPath" -ForegroundColor Green
} else {
    $missing += "OBS Studio 30+ (https://obsproject.com/)"
    Write-Host "  [MISS] OBS Studio not found at $obsPath" -ForegroundColor Red
}

# Git
try {
    $gitVer = git --version
    Write-Host "  [OK] Git: $gitVer" -ForegroundColor Green
} catch {
    $missing += "Git (https://git-scm.com/)"
    Write-Host "  [MISS] Git not found" -ForegroundColor Red
}

if ($missing.Count -gt 0) {
    Write-Host "`n[MISSING] 请安装以下工具后重试:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

# ---- 2. 准备 OBS SDK ----
Write-Host "`n[2/4] 准备 OBS SDK..." -ForegroundColor Yellow

$obsSdkDir = Join-Path $obsPath "SDK"
if (-not (Test-Path $obsSdkDir)) {
    Write-Host "  [INFO] OBS SDK 不在标准位置, 请确保安装了 OBS Studio 或手动指定 OBS_SDK_DIR" -ForegroundColor Magenta
    Write-Host "  下载: https://github.com/obsproject/obs-studio/releases" -ForegroundColor Magenta
} else {
    Write-Host "  [OK] OBS SDK: $obsSdkDir" -ForegroundColor Green
}

# ---- 3. 下载第三方依赖 ----
Write-Host "`n[3/4] 下载第三方依赖..." -ForegroundColor Yellow

$thirdPartyDir = Join-Path $PSScriptRoot "..\plugin\third_party"
New-Item -ItemType Directory -Force -Path $thirdPartyDir | Out-Null

# civetweb
$civetwebDir = Join-Path $thirdPartyDir "civetweb"
if (-not (Test-Path (Join-Path $civetwebDir "civetweb.c"))) {
    Write-Host "  下载 civetweb..." -ForegroundColor Gray
    New-Item -ItemType Directory -Force -Path $civetwebDir | Out-Null
    $civetwebUrl = "https://raw.githubusercontent.com/civetweb/civetweb/master/src/civetweb.c"
    $civetwebHdr = "https://raw.githubusercontent.com/civetweb/civetweb/master/include/civetweb.h"
    try {
        Invoke-WebRequest -Uri $civetwebUrl -OutFile (Join-Path $civetwebDir "civetweb.c")
        Invoke-WebRequest -Uri $civetwebHdr -OutFile (Join-Path $civetwebDir "civetweb.h")
        Write-Host "  [OK] civetweb" -ForegroundColor Green
    } catch {
        Write-Host "  [WARN] 无法下载 civetweb, 请手动放入 $civetwebDir" -ForegroundColor Yellow
    }
} else {
    Write-Host "  [OK] civetweb (已存在)" -ForegroundColor Green
}

# SQLite3 amalgamation
$sqliteDir = Join-Path $thirdPartyDir "sqlite3"
if (-not (Test-Path (Join-Path $sqliteDir "sqlite3.c"))) {
    Write-Host "  下载 SQLite3 amalgamation..." -ForegroundColor Gray
    New-Item -ItemType Directory -Force -Path $sqliteDir | Out-Null
    $sqliteUrl = "https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip"
    $sqliteZip = Join-Path $env:TEMP "sqlite3.zip"
    try {
        Invoke-WebRequest -Uri $sqliteUrl -OutFile $sqliteZip
        Expand-Archive -Path $sqliteZip -DestinationPath $sqliteDir -Force
        # Move files up if they're in a subfolder
        $extracted = Get-ChildItem $sqliteDir -Recurse -Filter "sqlite3.c" | Select-Object -First 1
        if ($extracted) {
            Copy-Item $extracted.FullName $sqliteDir
            Copy-Item (Join-Path $extracted.DirectoryName "sqlite3.h") $sqliteDir
            Copy-Item (Join-Path $extracted.DirectoryName "sqlite3ext.h") $sqliteDir
        }
        Remove-Item $sqliteZip -ErrorAction SilentlyContinue
        Write-Host "  [OK] SQLite3" -ForegroundColor Green
    } catch {
        Write-Host "  [WARN] 无法下载 SQLite3, 请手动放入 $sqliteDir" -ForegroundColor Yellow
    }
} else {
    Write-Host "  [OK] SQLite3 (已存在)" -ForegroundColor Green
}

# nlohmann/json (header-only)
$jsonDir = Join-Path $thirdPartyDir "json"
if (-not (Test-Path (Join-Path $jsonDir "single_include\nlohmann\json.hpp"))) {
    Write-Host "  下载 nlohmann/json..." -ForegroundColor Gray
    New-Item -ItemType Directory -Force -Path $jsonDir | Out-Null
    $jsonUrl = "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
    $jsonSingleDir = Join-Path $jsonDir "single_include\nlohmann"
    New-Item -ItemType Directory -Force -Path $jsonSingleDir | Out-Null
    try {
        Invoke-WebRequest -Uri $jsonUrl -OutFile (Join-Path $jsonSingleDir "json.hpp")
        Write-Host "  [OK] nlohmann/json" -ForegroundColor Green
    } catch {
        Write-Host "  [WARN] 无法下载 nlohmann/json, 请手动放入 $jsonDir" -ForegroundColor Yellow
    }
} else {
    Write-Host "  [OK] nlohmann/json (已存在)" -ForegroundColor Green
}

# ---- 4. 安装前端依赖 ----
Write-Host "`n[4/4] 安装前端依赖..." -ForegroundColor Yellow

$uiDir = Join-Path $PSScriptRoot "..\control-ui"
if (Test-Path (Join-Path $uiDir "package.json")) {
    Push-Location $uiDir
    try {
        npm install
        Write-Host "  [OK] npm install" -ForegroundColor Green
    } catch {
        Write-Host "  [WARN] npm install 失败, 请手动执行" -ForegroundColor Yellow
    }
    Pop-Location
} else {
    Write-Host "  [WARN] control-ui/package.json 未找到" -ForegroundColor Yellow
}

Write-Host "`n============================================" -ForegroundColor Cyan
Write-Host " 环境搭建完成!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "`n下一步: .\scripts\build.ps1" -ForegroundColor White
