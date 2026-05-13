# AGENTS.md — obs-multicam-review 项目开发守则

> **适用对象**：本项目中的所有 AI Agent（包括本 OpenClaw session）
> **版本**: v1.0 | **最后更新**: 2026-05-13

---

## 🔴 铁律（违反即事故）

### 1. 文档优先原则

**在做任何代码修改前，必须：**

```
步骤 1: 检查 docs/ 目录中是否存在相关规范文档
步骤 2: 如果需要新接口/新模块/新数据表 → 先更新 docs/API_SPEC.md 或对应的设计文档
步骤 3: 再进行代码实现
步骤 4: 代码改完后，检查文档是否与实际一致
```

**原因**：本项目由 AI 主导开发，文档是唯一的「统一真理源」。没有文档就没有统一标准。

### 2. 接口先行原则

- 任何前后端交互，**必须先定义 API 契约**，再分别实现
- API 定义在 `docs/API_SPEC.md`，前端和后端都按这个文件来
- 修改 API 必须同步更新 API_SPEC.md 和类型定义

### 3. 日志强制原则

- C++ 插件：使用 `blog_info` / `blog_warn` / `blog_error` 宏，**禁止** `printf`
- 前端：使用统一的 `logger` 模块（级别：debug/info/warn/error）
- 所有 API 请求/响应/错误必须记录

---

## 📁 关键文档索引

| 文档 | 路径 | 用途 |
|------|------|------|
| 架构设计 | `design/multicam-review-system-design.md` | 系统架构、功能矩阵 |
| **API 契约** | `docs/API_SPEC.md` | ⭐ 所有接口定义 |
| 编码规范 | `docs/CODING_STANDARDS.md` | C++ / TS 代码风格 |
| 调试指南 | `docs/DEBUG_GUIDE.md` | 调试流程 & 工具 |
| 开发路线图 | `design/multicam-review-system-design.md` §16 | 阶段规划 |

---

## 🔄 标准开发流程

### 新增功能

```
1. 阅读 docs/API_SPEC.md → 确认是否需要新 API
2. 更新 docs/API_SPEC.md → 定义新 API 契约
3. C++ 插件实现后端逻辑（plugin/src/）
4. React 前端实现对应界面（control-ui/src/）
5. 更新 docs/ 文档（如有必要）
6. 提交代码，commit message 标注修改的 API
```

### 修复 Bug

```
1. 定位问题 → 检查相关文档是否过时
2. 修复代码 → 同步修复文档
3. commit message 标注 "fix: xxx (closes #N)"
```

### 修改 API

```
1. ⚠️ 首先思考：能否向后兼容？（新增字段 OK，删除字段需版本迁移）
2. 更新 docs/API_SPEC.md
3. 同时更新插件端和前端
4. 版本号 bump
```

---

## 🏗️ 项目结构速查

```
obs-multicam-review/
├── AGENTS.md              ← 你在这里
├── docs/                  ← ⭐ 所有规范文档
│   ├── API_SPEC.md        ← 接口圣经
│   ├── CODING_STANDARDS.md
│   ├── DEBUG_GUIDE.md
│   └── ARCHITECTURE.md
├── design/                ← 系统设计文档（链接）
│   └── multicam-review-system-design.md
├── plugin/                ← C++ OBS 插件
│   ├── CMakeLists.txt
│   └── src/
│       ├── plugin.h       ← 全局上下文 + 模块声明
│       ├── main.cpp       ← 插件入口
│       └── [module]/      ← 各模块实现目录
│           ├── [module].h
│           └── [module].cpp
├── control-ui/            ← React 控制台
│   └── src/
│       ├── App.tsx
│       ├── api/           ← API 客户端封装
│       ├── pages/         ← 页面组件
│       ├── components/    ← 通用组件
│       ├── hooks/         ← 自定义 hooks
│       └── types/         ← TypeScript 类型（与 API_SPEC 对齐）
├── overlay/               ← 叠加层 HTML
└── scripts/               ← 构建/安装脚本
```

---

## 🧩 模块依赖关系

```
main.cpp (入口)
  ├── WebServer (civetweb)     → REST API + WebSocket
  ├── Recorder                 → 多路录制引擎
  ├── TimecodeGen              → SMPTE 时间码
  ├── SceneManager             → 场景/源管理
  ├── AudioConsole             → 调音台
  ├── Database (SQLite)        → 数据持久化
  └── PresetManager            → 预设导入/导出
```

---

## 📝 Commit 规范

```
<type>: <简短描述>

feat: 新增音频控制台模块
fix: 修复时间码偏移问题
docs: 更新 API_SPEC 评分接口
refactor: 重构源管理器数据结构
chore: 更新 CMake 依赖版本
```

---

## ⚠️ 常见陷阱

1. **宏/类型冲突**：OBS SDK 有大量宏（`T_`, `RGB`, `X`, `Y`, `BOOL`），避免在全局作用域用同名符号
2. **线程安全**：OBS 回调在多线程执行，访问共享数据必须加锁
3. **生命周期**：OBS source 引用计数，不用 `delete` 而是 `obs_source_release()`
4. **WebSocket**：前端必须处理断线重连，插件端必须处理客户端断开
5. **格式不匹配**：C++ 端 JSON 使用 `nlohmann/json`，字段名必须与 TypeScript 接口完全一致

---

> **记住：文档是项目的地图。没有地图就开车，一定会撞墙。**
