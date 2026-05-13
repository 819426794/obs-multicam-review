# GitHub 一键推送脚本
# 用法: .\scripts\push-github.ps1 -Token "ghp_xxx"
param(
    [Parameter(Mandatory=$true)]
    [string]$Token,

    [string]$Owner = "819426794",
    [string]$Repo = "obs-multicam-review"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " 推送到 GitHub: $Owner/$Repo" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# 目标 URL
$remoteUrl = "https://$Owner`:$Token@github.com/$Owner/$Repo.git"

# 初始化 git
Push-Location $projectRoot

# 检查是否已有 git
if (-not (Test-Path ".git")) {
    git init
    Write-Host "[OK] git init" -ForegroundColor Green
}

# 设置 remote
$existing = git remote get-url origin 2>$null
if ($existing) {
    if ($existing -notlike "*$Owner/$Repo*") {
        git remote set-url origin $remoteUrl
        Write-Host "[OK] remote updated" -ForegroundColor Green
    }
} else {
    git remote add origin $remoteUrl
    Write-Host "[OK] remote origin added" -ForegroundColor Green
}

# 创建 .gitkeep 让空目录被追踪
$keepDirs = @("plugin\third_party", "plugin\data\locale", "plugin\data\assets", "scripts")
foreach ($dir in $keepDirs) {
    $fullPath = Join-Path $projectRoot $dir
    New-Item -ItemType Directory -Force -Path $fullPath | Out-Null
    $gitkeep = Join-Path $fullPath ".gitkeep"
    if (-not (Test-Path $gitkeep)) {
        "" | Out-File $gitkeep -Encoding ASCII
    }
}

# 添加所有文件
git add -A

# 提交
$commitMsg = @"
🎬 Initial commit — 多机位评测录像系统 v0.1.0

项目骨架搭建:
- C++ OBS 插件 (libobs, CMake)
- React Web 控制台 (Vite + Tailwind)
- 叠加层 HTML (产品卡/排行榜/时间码)
- GitHub Actions CI/CD
- 完整架构设计文档 (v1.3, 17章节)
"@

git commit -m $commitMsg
Write-Host "[OK] Committed" -ForegroundColor Green

# 设置 main 分支
git branch -M main

# 推送
Write-Host "正在推送..." -ForegroundColor Yellow
git push -u origin main --force

# 创建 develop 分支
git checkout -b develop
git push -u origin develop

# 打标签
git checkout main
git tag -a "v0.1.0" -m "v0.1.0 — Phase 0: 项目初始化"
git push origin v0.1.0

Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host " 推送完成!" -ForegroundColor Green
Write-Host " https://github.com/$Owner/$Repo" -ForegroundColor White
Write-Host "============================================" -ForegroundColor Green

Pop-Location
