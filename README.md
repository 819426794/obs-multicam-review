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
├── AGENTS.md                  # ⭐ AI 开发守则（文档优先）
├── docs/                      # ⭐ 规范文档（接口真理源）
│   ├── API_SPEC.md            # REST + WebSocket 接口规范
│   ├── CODING_STANDARDS.md    # C++ / TypeScript 编码规范
│   └── DEBUG_GUIDE.md         # 调试指南
├── plugin/                    # C++ OBS 插件
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp           # 插件入口
│       └── plugin.h           # 全局上下文
├── control-ui/                # React Web 控制台
│   ├── package.json
│   └── src/
│       └── App.tsx
├── overlay/                   # 叠加层 HTML
│   └── product-card.html
├── scripts/
│   ├── setup.ps1
│   ├── build.ps1
│   └── install.ps1
└── README.md
```

## 📚 开发文档

| 文档 | 用途 |
|------|------|
| [AGENTS.md](AGENTS.md) | ⭐ **AI 开发守则** — 修改代码前必须先读 |
| [docs/API_SPEC.md](docs/API_SPEC.md) | **接口契约** — REST/WS 接口定义（真理源） |
| [docs/CODING_STANDARDS.md](docs/CODING_STANDARDS.md) | **编码规范** — C++/TS 风格约定 |
| [docs/DEBUG_GUIDE.md](docs/DEBUG_GUIDE.md) | **调试指南** — 调试流程 & 排查 |
| [design/multicam-review-system-design.md](../design/multicam-review-system-design.md) | **架构设计** — v1.3 完整设计文档 |

> ⚠️ **AI 开发守则**：任何功能开发都必须遵循 **文档优先 → 接口先行 → 代码实现** 的顺序。

## 许可

GPLv2 — 开源可商用
