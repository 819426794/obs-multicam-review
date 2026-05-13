# CI 环境下无需交互，静默下载第三方库
param()

$ErrorActionPreference = "Stop"
$thirdPartyDir = Join-Path $PSScriptRoot "..\plugin\third_party"

Write-Host "[CI] Setting up third-party libraries..."

# civetweb
$civetwebDir = Join-Path $thirdPartyDir "civetweb"
if (-not (Test-Path (Join-Path $civetwebDir "civetweb.c"))) {
    Write-Host "  civetweb..."
    New-Item -ItemType Directory -Force -Path $civetwebDir | Out-Null
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/civetweb/civetweb/master/src/civetweb.c" -OutFile (Join-Path $civetwebDir "civetweb.c")
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/civetweb/civetweb/master/include/civetweb.h" -OutFile (Join-Path $civetwebDir "civetweb.h")
}

# SQLite3
$sqliteDir = Join-Path $thirdPartyDir "sqlite3"
if (-not (Test-Path (Join-Path $sqliteDir "sqlite3.c"))) {
    Write-Host "  SQLite3..."
    New-Item -ItemType Directory -Force -Path $sqliteDir | Out-Null
    Invoke-WebRequest -Uri "https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip" -OutFile "$env:TEMP\sqlite3.zip"
    Expand-Archive -Path "$env:TEMP\sqlite3.zip" -DestinationPath $sqliteDir -Force
    $extracted = Get-ChildItem $sqliteDir -Recurse -Filter "sqlite3.c" | Select-Object -First 1
    if ($extracted) {
        Copy-Item $extracted.FullName $sqliteDir
        Copy-Item (Join-Path $extracted.DirectoryName "sqlite3.h") $sqliteDir
        Copy-Item (Join-Path $extracted.DirectoryName "sqlite3ext.h") $sqliteDir
    }
}

# nlohmann/json
$jsonDir = Join-Path $thirdPartyDir "json"
if (-not (Test-Path (Join-Path $jsonDir "single_include\nlohmann\json.hpp"))) {
    Write-Host "  nlohmann/json..."
    $jsonSingleDir = Join-Path $jsonDir "single_include\nlohmann"
    New-Item -ItemType Directory -Force -Path $jsonSingleDir | Out-Null
    Invoke-WebRequest -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -OutFile (Join-Path $jsonSingleDir "json.hpp")
}

Write-Host "  [DONE]"
