# 多机位评测录像系统 (obs-multicam-review)

> OBS Studio 插件 — 一站式多机位评测录制解决方案

## 环境要求

| 工具 | 最低版本 | 用途 |
|------|---------|------|
| Visual Studio 2022 | 17.x | C++ 编译 (MSVC) |
| CMake | 3.24+ | 构建系统 |
| OBS Studio | 30.x | 插件宿主 |
| OBS SDK (libobs) | 30.x | 编译依赖 |
| Node.js | 18+ | React 前端构建 |
| Git | any | 版本控制 |

## 快速开始

```powershell
# 1. 安装依赖
.\scripts\setup.ps1

# 2. 构建插件
.\scripts\build.ps1

# 3. 安装到 OBS
.\scripts\install.ps1

# 4. 启动 OBS Studio，插件自动加载
```

## Git + GitHub 初始化

```powershell
# 一条命令完成: git init, commit, 创建 GitHub 仓库, 推送
.\scripts\git-init.ps1 -GitHubUser "你的GitHub用户名"

# 跳过推送（仅本地 git init）
.\scripts\git-init.ps1 -GitHubUser "xxx" -SkipPush

# 创建私有仓库
.\scripts\git-init.ps1 -GitHubUser "xxx" -Private
```

### 分支策略

```
main        ← Release 分支，只合入 develop，打 tag
develop     ← 日常开发，从这里切 feature
feature/*   ← 新功能分支
hotfix/*    ← 紧急修复
release/*   ← 发布准备
```

### 版本号 (SemVer)

| 版本 | 阶段 |
|------|------|
| `v0.1.0` | Phase 0-3 (初始化+核心框架) |
| `v0.2.0` | Phase 4-6 (音频+叠加层+转场) |
| `v0.3.0` | Phase 7-9 (评分+快捷键+重渲染) |
| `v1.0.0-rc1` | 发布候选 |
| `v1.0.0` | 首个正式发布 |

发布时打 tag → CI 自动构建 Release zip

## 项目结构

```
obs-multicam-review/
├── plugin/                    # C++ OBS 插件
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp           # 插件入口
│   │   ├── websocket_server.cpp
│   │   ├── recorder.cpp
│   │   ├── timecode.cpp
│   │   ├── scene_manager.cpp
│   │   ├── db_manager.cpp
│   │   └── ...
│   ├── third_party/
│   │   └── civetweb/          # HTTP/WS 服务器
│   └── data/
│       ├── locale/            # 多语言
│       └── assets/            # 内置资源
├── control-ui/                # React Web 控制台
│   ├── package.json
│   ├── vite.config.ts
│   ├── src/
│   │   ├── App.tsx
│   │   ├── pages/
│   │   ├── components/
│   │   └── lib/
│   └── ...
├── overlay/                   # 叠加层 HTML
│   ├── index.html
│   ├── product-card.html
│   └── leaderboard.html
├── scripts/
│   ├── setup.ps1
│   ├── build.ps1
│   └── install.ps1
└── README.md
```

## 许可

GPLv2 — 开源可商用
