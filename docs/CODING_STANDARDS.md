# 编码规范 — obs-multicam-review

> **版本**: v1.1 | **适用**: C++ (MSVC/CMake) + TypeScript (React/Vite)

---

## 目录

1. [C++ 规范](#1-c-规范)
   - 1.7 [SourceRecordFilter 模块](#17-sourcerecordfilter-模块)
   - 1.8 [WebSocket 通信函数](#18-websocket-通信函数)
2. [TypeScript / React 规范](#2-typescript--react-规范)
3. [JSON / API 数据规范](#3-json--api-数据规范)
4. [文件组织规范](#4-文件组织规范)
   - 4.3 [Markdown 文件规范](#43-markdown-文件规范)

---

## 1. C++ 规范

### 1.1 风格基线

| 规则 | 约定 |
|------|------|
| 标准 | C++17 |
| 缩进 | 4 空格 |
| 行宽 | 120 字符 |
| 命名空间 | `multicam::` |
| 头文件保护 | `#pragma once` |
| 内存 | 优先 RAII；OBS 对象用引用计数 |
| 分配 | OBS 内部用 `bzalloc`/`bfree`，模块内用 `std::unique_ptr` |

### 1.2 命名约定

```cpp
// ✅ 正确
class SceneManager;          // PascalCase 类型
void start_recording();      // snake_case 函数
int frame_count_;            // snake_case_ 成员变量
constexpr int kMaxSources;   // kPascalCase 常量
#define PLUGIN_NAME "..."    // UPPER_SNAKE_CASE 宏

// ❌ 错误
class sceneManager;          // camelCase
void StartRecording();       // PascalCase 函数
int frameCount;              // camelCase 成员（OBS 惯用 snake_case）
```

### 1.3 包含顺序

```cpp
// 1. 本模块头文件
#include "plugin.h"

// 2. OBS SDK
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

// 3. 第三方
#include <nlohmann/json.hpp>
#include <sqlite3.h>

// 4. C++ 标准库
#include <string>
#include <vector>
#include <memory>
#include <mutex>
```

### 1.4 日志规范

```cpp
// ✅ 使用插件统一前缀
blog_info ("[module] action started");
blog_warn ("[module] unexpected value: %d", val);
blog_error("[module] failed: %s", reason);

// ❌ 禁止
printf(...);
std::cout << "...";
OutputDebugStringA(...);  // 仅在调试时可用
```

### 1.5 线程安全

```cpp
class Example {
    std::mutex mutex_;
    std::vector<int> data_;
public:
    void add(int v) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.push_back(v);
    }
};
```

- OBS 信号回调在**图形线程**执行 → 不要阻塞
- 耗时操作（编码/文件IO）放入**工作线程**
- WebSocket 回调在 **civetweb 线程**

### 1.6 OBS 对象生命周期

```cpp
// ✅ 正确：引用计数
obs_source_t *src = obs_source_create("type", "name", nullptr, nullptr);
obs_source_addref(src);    // 需要时增加引用
obs_source_release(src);   // 用完后释放

// ❌ 错误
delete src;                // OBS 对象不是普通指针
```

### 1.7 SourceRecordFilter 模块

新增滤镜模块时需在插件加载/卸载时注册/注销 OBS 源类型：

```cpp
// obs_module_load() 中注册
obs_register_source(&source_record_filter_info);

// obs_module_unload() 中注销
// OBS 自动处理 obs_source_info 注册的源类型，
// 但需确保 obs_source_info 结构体为 static 生命周期
```

### 1.8 WebSocket 通信函数

使用专用函数进行实时通信，命名遵循 `ws_` 前缀约定：

```cpp
// 发送消息给单个客户端
void ws_send(struct mg_connection *conn, const std::string &msg);

// 广播事件给所有已连接客户端
void ws_broadcast_event(const std::string &event_type, const nlohmann::json &payload);
```

> 所有 WebSocket 消息必须通过这两个函数发送，禁止直接调用 `mg_websocket_write()`。

---

## 2. TypeScript / React 规范

### 2.1 风格基线

| 规则 | 约定 |
|------|------|
| 类型 | 严格模式 `"strict": true` |
| 缩进 | 2 空格 |
| 引号 | 单引号 `'` |
| 分号 | 必须 |
| 组件 | 函数组件 + Hooks，禁止 class 组件 |

### 2.2 命名约定

```typescript
// ✅ 正确
type RecordingState = 'idle' | 'recording' | 'paused';  // PascalCase 类型
interface SourceInfo { ... }                              // PascalCase 接口
const MAX_SOURCES = 16;                                   // UPPER_SNAKE_CASE 常量
function useRecording(): UseRecordingReturn { ... }       // camelCase 函数
const RecordingControl: React.FC = () => { ... };          // PascalCase 组件

// ❌ 错误
type recordingState = ...;    // camelCase 类型
function UseRecording() { }   // PascalCase hook
```

### 2.3 组件结构模板

```typescript
// components/RecordingControl.tsx
import React, { useState, useCallback } from 'react';
import { useRecording } from '../hooks/useRecording';
import type { RecordingState } from '../types/recording';

interface RecordingControlProps {
  sceneId: string;
  onStateChange?: (state: RecordingState) => void;
}

export const RecordingControl: React.FC<RecordingControlProps> = ({
  sceneId,
  onStateChange,
}) => {
  const { state, start, stop } = useRecording(sceneId);

  const handleToggle = useCallback(() => {
    if (state === 'recording') {
      stop();
    } else {
      start();
    }
    onStateChange?.(state);
  }, [state, start, stop, onStateChange]);

  return (
    <button onClick={handleToggle}>
      {state === 'recording' ? '停止' : '录制'}
    </button>
  );
};
```

### 2.4 API 调用规范

```typescript
// api/client.ts — 统一封装，禁止直接在组件中 fetch

import type { RecordingResult } from '../types/api';

const API_BASE = 'http://localhost:9527/api';

export async function startRecording(): Promise<RecordingResult> {
  const res = await fetch(`${API_BASE}/rec/start`, { method: 'POST' });
  if (!res.ok) throw new ApiError(res.status, await res.text());
  return res.json();
}

// 所有请求必须处理超时和错误
export class ApiError extends Error {
  constructor(public status: number, message: string) {
    super(`API ${status}: ${message}`);
  }
}
```

### 2.5 目录结构

```
control-ui/src/
├── api/          ← API 客户端（按模块分文件）
│   ├── client.ts     ← 基础请求封装 + 错误处理
│   ├── recording.ts  ← 录制相关 API
│   ├── sources.ts    ← 输入源相关 API
│   └── ...
├── types/        ← 类型定义（与 API_SPEC 对齐）
│   └── api.ts        ← API 请求/响应类型
├── hooks/        ← 自定义 hooks
├── components/   ← 通用组件（可复用）
├── pages/        ← 页面组件（一个路由一个页面）
└── store/        ← 全局状态（如需要 Zustand）
```

---

## 3. JSON / API 数据规范

### 3.1 字段命名

```json
{
  // ✅ camelCase
  "sourceId": "cam_01",
  "frameRate": 60,
  "audioLevels": [-12, -8, 0],

  // ❌ 禁止
  "source_id": "cam_01",      // snake_case
  "SourceId": "cam_01",       // PascalCase
  "frame-rate": 60            // kebab-case
}
```

### 3.2 时间戳格式

```typescript
// 统一使用 ISO 8601 + 毫秒 + 时区
type Timestamp = string;  // "2026-05-13T12:30:00.000+08:00"

// 时间码（SMPTE）
type Timecode = string;   // "01:23:45:06"  (HH:MM:SS:FF)
```

### 3.3 枚举值

```typescript
// 统一使用字符串枚举，禁止数字枚举
type RecordingState = 'idle' | 'recording' | 'paused';  // ✅
enum RecordingState { Idle = 0, Recording = 1 }           // ❌
```

---

## 4. 文件组织规范

### 4.1 模块目录（C++）

```
plugin/src/
├── plugin.h               ← 全局上下文
├── main.cpp               ← 入口
├── webserver/             ← Web 服务器模块
│   ├── webserver.h
│   ├── webserver.cpp
│   ├── api_handler.cpp    ← REST API 路由
│   └── ws_handler.cpp     ← WebSocket 处理
├── recorder/              ← 录制引擎
│   ├── recorder.h
│   └── recorder.cpp
├── database/              ← 数据库层
│   ├── database.h
│   └── database.cpp
└── ...
```

> 每个模块 i 必须有一个 `.h` 头文件和一个或多个 `.cpp` 实现文件。  
> 模块间通过 `.h` 中的公共接口通信，禁止 `extern` 跨文件访问。

### 4.2 文件命名

| 语言 | 命名 | 示例 |
|------|------|------|
| C++ | snake_case | `scene_manager.h` |
| TypeScript | kebab-case 或 camelCase | `recording-control.tsx` / `useRecording.ts` |
| CSS | kebab-case | `recording-panel.css` |
| 文档 | UPPER_SNAKE_CASE | `API_SPEC.md` |

### 4.3 Markdown 文件规范

- 第一个 `#` 标题应仅包含文件名（符合 GitHub 渲染习惯），例如：`# API_SPEC.md — obs-multicam-review`
- 后续标题使用 `##`、`###` 等层级
- 代码块必须标注语言：`` ````cpp` ````、`` ````typescript` ````、`` ````json` ````
- 表格用 `|` 对齐，表头与内容用 `---` 分隔

---

## 📚 参考

- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)（C++17 适用部分）
- [Airbnb React/JSX Style Guide](https://github.com/airbnb/javascript/tree/master/react)
- OBS Studio 源码风格：`snake_case` 函数、4 空格缩进、`#pragma once`
