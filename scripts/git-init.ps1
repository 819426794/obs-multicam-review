# Git + GitHub 完整初始化脚本
# 用法: .\scripts\git-init.ps1 -GitHubUser "你的用户名"
param(
    [Parameter(Mandatory=$true)]
    [string]$GitHubUser,

    [string]$RepoName = "obs-multicam-review",
    [string]$RepoDescription = "多机位评测录像系统 — OBS Studio 插件",
    [switch]$Private = $false,
    [switch]$SkipPush = $false
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Git + GitHub 仓库初始化" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# ---- 1. 创建 CHANGELOG ----
Write-Host "`n[1/6] 创建 CHANGELOG.md..." -ForegroundColor Yellow

$changelog = @"
# Changelog

All notable changes to obs-multicam-review will be documented in this file.

## [Unreleased]

### Added
- 完整预设管理系统（保存/导出/导入）
- 音频控制台（独立音量/路由/效果/监听）
- 不限数量输入源管理（HDMI/SDI/IP/NDI/桌面/窗口/网页/媒体文件）
- PPT 演示控制集成
- 多评委评分系统，每人独立评分、独立维度
- 每产品可自定义评分维度模板

## [0.1.0] — Phase 0

项目初始化，开发环境搭建。

### Infrastructure
- CMake 构建系统
- React + Vite + Tailwind 控制台前端
- civetweb HTTP/WebSocket 服务器
- SQLite 数据库
- GitHub Actions CI/CD
"@

Set-Content -Path (Join-Path $projectRoot "CHANGELOG.md") -Value $changelog -Encoding UTF8
Write-Host "  [OK] CHANGELOG.md" -ForegroundColor Green

# ---- 2. Git 初始化 ----
Write-Host "`n[2/6] 初始化 Git 仓库..." -ForegroundColor Yellow

Push-Location $projectRoot
try {
    git init
    Write-Host "  [OK] git init" -ForegroundColor Green
} catch {
    Write-Host "  [ERROR] git 未安装或不可用" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ---- 3. 配置 Git 用户 (如果未设置) ----
Write-Host "`n[3/6] 检查 Git 用户配置..." -ForegroundColor Yellow

$gitName = git config --global user.name
$gitEmail = git config --global user.email

if (-not $gitName) {
    $inputName = Read-Host "  输入你的名字 (git config user.name)"
    git config --global user.name $inputName
    Write-Host "  [OK] user.name = $inputName" -ForegroundColor Green
} else {
    Write-Host "  [OK] user.name = $gitName" -ForegroundColor Green
}

if (-not $gitEmail) {
    $inputEmail = Read-Host "  输入你的邮箱 (git config user.email)"
    git config --global user.email $inputEmail
    Write-Host "  [OK] user.email = $inputEmail" -ForegroundColor Green
} else {
    Write-Host "  [OK] user.email = $gitEmail" -ForegroundColor Green
}

# ---- 4. 首次提交 ----
Write-Host "`n[4/6] 首次提交..." -ForegroundColor Yellow

# 确保 .gitkeep 存在让空目录也被追踪
$keepDirs = @(
    "plugin\third_party",
    "plugin\data\locale",
    "plugin\data\assets",
    "overlay",
    "scripts"
)
foreach ($dir in $keepDirs) {
    $fullPath = Join-Path $projectRoot $dir
    New-Item -ItemType Directory -Force -Path $fullPath | Out-Null
    $gitkeep = Join-Path $fullPath ".gitkeep"
    if (-not (Test-Path $gitkeep)) {
        "" | Out-File $gitkeep -Encoding ASCII
    }
}

git add -A
git status

$commitMsg = "🎬 Initial commit — 多机位评测录像系统 v0.1.0

项目骨架搭建:
- C++ OBS 插件 (libobs, CMake)
- React Web 控制台 (Vite + Tailwind)
- 叠加层 HTML (产品卡/排行榜/时间码)
- GitHub Actions CI/CD
- 完整架构设计文档 (v1.3, 17章节)"

git commit -m $commitMsg
Write-Host "  [OK] Initial commit" -ForegroundColor Green

# ---- 5. 创建 develop 分支 ----
Write-Host "`n[5/6] 创建 develop 分支..." -ForegroundColor Yellow

git checkout -b develop
git checkout main
Write-Host "  [OK] 分支: main + develop" -ForegroundColor Green

# ---- 6. 推送到 GitHub ----
if (-not $SkipPush) {
    Write-Host "`n[6/6] 创建 GitHub 仓库并推送..." -ForegroundColor Yellow

    $remoteUrl = "https://github.com/$GitHubUser/$RepoName.git"

    # 检查是否已有 remote
    $existingRemote = git remote get-url origin 2>$null
    if ($existingRemote) {
        Write-Host "  remote origin 已存在: $existingRemote" -ForegroundColor Gray
        if ($existingRemote -ne $remoteUrl) {
            Write-Host "  更新为: $remoteUrl" -ForegroundColor Gray
            git remote set-url origin $remoteUrl
        }
    } else {
        git remote add origin $remoteUrl
    }

    Write-Host "`n  ╔══════════════════════════════════════════╗" -ForegroundColor Magenta
    Write-Host "  ║  请先在浏览器中创建 GitHub 仓库:          ║" -ForegroundColor Magenta
    Write-Host "  ║                                          ║" -ForegroundColor Magenta
    Write-Host "  ║  https://github.com/new                  ║" -ForegroundColor Magenta
    Write-Host "  ║                                          ║" -ForegroundColor Magenta
    Write-Host "  ║  Repository name: $RepoName" -ForegroundColor White
    Write-Host "  ║  Description: $RepoDescription" -ForegroundColor White
    if ($Private) {
        Write-Host "  ║  Private: Yes" -ForegroundColor White
    } else {
        Write-Host "  ║  Private: No (Public)" -ForegroundColor White
    }
    Write-Host "  ║  ❌ 不要勾选 README/ .gitignore / License" -ForegroundColor Yellow
    Write-Host "  ║  ❌ 保持空仓库创建                         ║" -ForegroundColor Yellow
    Write-Host "  ║                                          ║" -ForegroundColor Magenta
    Write-Host "  ║  创建完成后按 Enter 继续推送...            ║" -ForegroundColor Magenta
    Write-Host "  ╚══════════════════════════════════════════╝" -ForegroundColor Magenta

    Read-Host

    Write-Host "  正在推送 main + develop..." -ForegroundColor Gray
    git branch -M main
    git push -u origin main
    git push -u origin develop

    # 创建 v0.1.0 标签
    git tag -a "v0.1.0" -m "v0.1.0 — Phase 0: 项目初始化"
    git push origin v0.1.0

    Write-Host "  [OK] 推送完成!" -ForegroundColor Green
    Write-Host "  Repo: https://github.com/$GitHubUser/$RepoName" -ForegroundColor White
} else {
    Write-Host "`n[6/6] 跳过推送 (--SkipPush)" -ForegroundColor Yellow
}

Pop-Location

# ---- 版本管理策略说明 ----
Write-Host "`n============================================" -ForegroundColor Cyan
Write-Host " 版本管理 & 备份策略" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

Write-Host @"

分支策略 (Git Flow):
  main        ← Release 分支，只合并 develop，打 tag
  develop     ← 日常开发分支，从这里切 feature
  feature/*   ← 新功能分支，合入 develop
  hotfix/*    ← 紧急修复，从 main 切，合入 main+develop
  release/*   ← 发布准备，从 develop 切，合入 main+develop

版本号规则 (SemVer):
  MAJOR.MINOR.PATCH
  1.0.0  ← 首个正式发布
  0.1.0  ← Phase 0-3 (开发中)
  0.2.0  ← Phase 4-6
  0.3.0  ← Phase 7-9
  1.0.0-rc1 ← 发布候选

标签规则:
  git tag -a "v1.0.0" -m "正式发布"
  git tag -a "v0.1.0-alpha" -m "内部测试版"

备份策略:
  ✅ GitHub (主仓库) — 自动备份所有代码 + 历史
  ✅ GitHub Releases — 打包 DLL + Web UI 的 zip
  ✅ GitHub Actions artifact — 每次 CI 构建产物保留 90 天
  📋 建议: 定期 git bundle 到本地 NAS/U盘

常用操作:
  # 创建功能分支
  git checkout -b feature/audio-console develop
  # ...开发...
  git add -A && git commit -m "feat(audio): 音频控制台独立音量路由"
  git checkout develop && git merge feature/audio-console
  git push origin develop

  # 发布新版本
  git checkout develop && git pull
  git checkout -b release/1.0.0 develop
  # 更新 CHANGELOG.md
  git add CHANGELOG.md && git commit -m "chore: bump to 1.0.0"
  git checkout main && git merge release/1.0.0
  git tag -a v1.0.0 -m "v1.0.0"
  git push origin main --tags
  # CI 自动构建 Release 并上传 artifact

"@
