# 构建脚本
param(
    [string]$BuildType = "RelWithDebInfo",
    [string]$ObsSdkDir = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " 构建 obs-multicam-review" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "Build Type: $BuildType" -ForegroundColor Gray

# ---- 1. 构建 React 前端 ----
Write-Host "`n[1/3] 构建 Web 控制台..." -ForegroundColor Yellow

$uiDir = Join-Path $projectRoot "control-ui"
if (Test-Path (Join-Path $uiDir "package.json")) {
    Push-Location $uiDir
    try {
        npm run build
        Write-Host "  [OK] Web UI built → control-ui/dist/" -ForegroundColor Green
    } catch {
        Write-Host "  [ERROR] Web UI 构建失败" -ForegroundColor Red
        Pop-Location
        exit 1
    }
    Pop-Location
} else {
    Write-Host "  [SKIP] control-ui/package.json not found" -ForegroundColor Yellow
}

# ---- 2. CMake 配置 ----
Write-Host "`n[2/3] CMake 配置..." -ForegroundColor Yellow

$buildDir = Join-Path $projectRoot "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$cmakeArgs = @(
    "-S", (Join-Path $projectRoot "plugin"),
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=$BuildType"
)

if ($ObsSdkDir) {
    $cmakeArgs += "-DOBS_SDK_DIR=$ObsSdkDir"
}

Push-Location $buildDir
try {
    $cmakeCmd = "cmake $($cmakeArgs -join ' ')"
    Write-Host "  $cmakeCmd" -ForegroundColor Gray
    Invoke-Expression $cmakeCmd
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed"
    }
    Write-Host "  [OK] CMake configure" -ForegroundColor Green
} catch {
    Write-Host "  [ERROR] CMake 配置失败" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location

# ---- 3. 编译 ----
Write-Host "`n[3/3] 编译插件..." -ForegroundColor Yellow

Push-Location $buildDir
try {
    cmake --build . --config $BuildType --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
    Write-Host "  [OK] 编译完成" -ForegroundColor Green

    # 显示产物
    $dllPath = Join-Path $buildDir $BuildType "obs-multicam-review.dll"
    if (Test-Path $dllPath) {
        $dllInfo = Get-Item $dllPath
        Write-Host "  产物: $dllPath ($([math]::Round($dllInfo.Length/1024,1)) KB)" -ForegroundColor White
    }
} catch {
    Write-Host "  [ERROR] 编译失败" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location

Write-Host "`n============================================" -ForegroundColor Cyan
Write-Host " 构建完成!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "`n下一步: .\scripts\install.ps1" -ForegroundColor White
