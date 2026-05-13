# API 接口规范 — obs-multicam-review

> **版本**: v0.1.0 | **协议**: REST + WebSocket | **消息格式**: JSON  
> ⚠️ **此文档是前后端接口的唯一真理源。新增/修改接口必须先更新本文档。**

---

## 目录

1. [通信架构](#1-通信架构)
2. [通用约定](#2-通用约定)
3. [REST API](#3-rest-api)
4. [WebSocket 协议](#4-websocket-协议)
5. [TypeScript 类型定义](#5-typescript-类型定义)
6. [错误码](#6-错误码)

---

## 1. 通信架构

```
┌─────────────────────┐         REST API (HTTP)
│   React 控制台       │◄────────► :9527/api/*
│ (control-ui)        │         WebSocket
│                      │◄────────► :9527/ws
└──────────┬──────────┘
           │ (同页面 DOM Event)
           ▼
┌─────────────────────┐         WebSocket
│   叠加层页面          │◄────────► :9527/ws
│ (overlay/*.html)     │
└─────────────────────┘

后端：C++ OBS 插件（civetweb 嵌入式 HTTP/WS 服务器）
端口：9527
```

### 通信规则

| 规则 | 说明 |
|------|------|
| REST 用于 | 控制台 → 插件的**操作指令**（录制启停、场景切换） |
| WebSocket 用于 | 插件的**实时状态推送**（时间码、录制状态）、控制台 → 插件的**实时操作** |
| 超时 | REST 请求 5 秒超时，WebSocket 心跳 15 秒 |
| 重连 | 前端 WebSocket 断开后自动重连（指数退避，最大间隔 30 秒） |
| 认证 | Phase 1 无认证，Phase 2+ 用 token |

---

## 2. 通用约定

### 2.1 消息信封

```json
{
  "type": "request | response | event",
  "id": "uuid-v4",
  "action": "action.name",
  "payload": {},
  "timestamp": "2026-05-13T12:30:00.000+08:00",
  "error": {
    "code": "ERR_OBS_RECORDING_ACTIVE",
    "message": "录制已在进行中"
  }
}
```

- `type=request`：控制台 → 插件（需要响应）
- `type=response`：插件 → 控制台（对 request 的回复）
- `type=event`：插件 → 控制台/叠加层（主动推送）
- `id`：request 和对应 response 共享同一 id
- `error`：仅在出错时存在

### 2.2 命名约定

| 约定 | 示例 |
|------|------|
| API 路径 | `/api/模块/动作`，全小写 |
| action 名称 | `模块.动作`，小写点分隔 |
| 字段名 | camelCase（JSON） |
| 枚举值 | 小写字符串 |
| 时间码 | `HH:MM:SS:FF`（NDF）|
| 时间戳 | ISO 8601 + 时区 |

### 2.3 状态码（REST）

| 状态 | 含义 |
|------|------|
| 200 | 成功 |
| 400 | 请求参数错误 |
| 409 | 状态冲突（如录制中再次请求录制） |
| 500 | 插件内部错误 |

---

## 3. REST API

### 3.1 录制控制

#### `POST /api/rec/start`
启动录制

**请求体**：`{}`（Phase 1 无参数，使用当前预设配置）

**响应 200**：
```json
{
  "recordingId": "rec_20260513_123000",
  "startTime": "2026-05-13T12:30:00.000+08:00",
  "outputDir": "C:\\Recordings\\2026-05-13\\"
}
```

**响应 409**：
```json
{ "error": { "code": "ERR_REC_ALREADY_ACTIVE", "message": "录制已在进行中" } }
```

---

#### `POST /api/rec/stop`
停止录制

**请求体**：`{}`

**响应 200**：
```json
{
  "recordingId": "rec_20260513_123000",
  "duration": 125.5,
  "files": [
    { "sourceId": "cam_main", "path": "C:\\Recordings\\2026-05-13\\cam_main_123000.mp4", "size": 524288000 },
    { "sourceId": "cam_top", "path": "C:\\Recordings\\2026-05-13\\cam_top_123000.mp4", "size": 498073600 }
  ]
}
```

---

#### `POST /api/rec/pause`
暂停录制

#### `POST /api/rec/resume`
恢复录制

---

### 3.2 场景控制

#### `POST /api/scene/switch`
切换场景

```json
{
  "sceneName": "main_pip",
  "transitionId": "fade"      // 可选，默认 cut
}
```

#### `GET /api/scene/list`
获取场景列表

**响应 200**：
```json
{
  "currentScene": "main_full",
  "scenes": [
    { "name": "main_full", "sources": ["cam_main"] },
    { "name": "main_pip", "sources": ["cam_main", "cam_top"] },
    { "name": "split_4", "sources": ["cam_main", "cam_top", "cam_side", "cam_detail"] }
  ]
}
```

---

### 3.3 输入源控制

#### `GET /api/source/list`
获取所有输入源

**响应 200**：
```json
{
  "sources": [
    {
      "id": "cam_main",
      "name": "主镜头",
      "type": "dshow_input",
      "active": true,
      "resolution": { "width": 1920, "height": 1080 },
      "fps": 60,
      "audioLevel": -6.5
    }
  ]
}
```

#### `POST /api/source/show`
显示输入源

```json
{ "sourceName": "cam_top" }
```

#### `POST /api/source/hide`
隐藏输入源

```json
{ "sourceName": "cam_top" }
```

#### `POST /api/source/configure`
配置输入源属性

```json
{
  "sourceName": "cam_main",
  "properties": {
    "resolution": "1920x1080",
    "fps": 60,
    "videoFormat": "MJPG"
  }
}
```

---

### 3.4 打点/标记

#### `POST /api/marker/add`
添加时间标记

```json
{
  "name": "关键节点",
  "type": "manual",
  "metadata": { "note": "产品展示开始" }
}
```

**响应 200**：
```json
{
  "id": "mrk_001",
  "name": "关键节点",
  "timecode": "01:23:45:06",
  "timestamp": "2026-05-13T12:31:05.000+08:00"
}
```

#### `GET /api/marker/list?recordingId=rec_20260513_123000`
获取某次录制的所有标记

---

### 3.5 预设管理

#### `GET /api/preset/list`
获取预设列表

#### `POST /api/preset/save`
保存当前配置为预设

```json
{ "presetName": "四镜头评测模式", "overwrite": false }
```

#### `POST /api/preset/load`
加载预设

```json
{ "presetId": "pre_001" }
```

#### `POST /api/preset/delete`
删除预设

#### `POST /api/preset/export`
导出预设计文件

#### `POST /api/preset/import`
导入预设计文件（multipart/form-data）

---

### 3.6 系统状态

#### `GET /api/system/status`
获取系统状态

**响应 200**：
```json
{
  "pluginVersion": "1.0.0",
  "obsVersion": "30.2.0",
  "recordingState": "idle",
  "currentScene": "main_full",
  "timecode": "01:23:45:06",
  "fps": 60,
  "diskFreeBytes": 262144000000,
  "cpuUsage": 12.5,
  "memoryUsageMB": 256
}
```

#### `GET /api/system/health`
健康检查

**响应 200**：
```json
{ "status": "ok", "uptime": 3600 }
```

---

## 4. WebSocket 协议

### 4.1 连接

```
ws://localhost:9527/ws
```

连接建立后，插件立即发送 `system.status` 事件。

### 4.2 心跳

| 方向 | action | 间隔 |
|------|--------|------|
| 插件 → 客户端 | `system.heartbeat` | 15 秒 |
| 客户端 → 插件 | `system.pong` | 收到 heartbeat 后回复 |

如果 30 秒内未收到 `system.pong`，插件主动关闭连接。

### 4.3 事件（插件 → 客户端）

| action | 触发时机 | payload |
|--------|---------|---------|
| `recording.started` | 录制开始 | `recordingId`, `startTime`, `outputDir` |
| `recording.stopped` | 录制停止 | `recordingId`, `duration`, `files[]` |
| `recording.paused` | 录制暂停 | `recordingId` |
| `recording.resumed` | 录制恢复 | `recordingId` |
| `timecode.tick` | 每帧（60fps） | `timecode`, `frameIndex`, `fps` |
| `source.status` | 源状态变化 | `sourceId`, `active`, `resolution`, `fps`, `audioLevel` |
| `source.added` | 新输入源加入 | 完整源对象 |
| `source.removed` | 输入源移除 | `sourceId` |
| `marker.added` | 新标记 | 完整标记对象 |
| `scene.changed` | 场景切换 | `sceneName` |
| `audio.level` | 音频电平（10Hz） | `sourceId`, `levels[]` |
| `system.status` | 连接建立时 | 完整系统状态 |
| `system.heartbeat` | 每 15 秒 | `{}` |
| `system.error` | 运行错误 | `code`, `message` |

### 4.4 指令（客户端 → 插件）

| action | 功能 | payload |
|--------|------|---------|
| `rec.start` | 启动录制 | `{}` |
| `rec.stop` | 停止录制 | `{}` |
| `rec.pause` | 暂停录制 | `{}` |
| `rec.resume` | 恢复录制 | `{}` |
| `scene.switch` | 切换场景 | `{ "sceneName": "main_pip", "transitionId": "fade" }` |
| `source.show` | 显示源 | `{ "sourceName": "cam_top" }` |
| `source.hide` | 隐藏源 | `{ "sourceName": "cam_top" }` |
| `marker.add` | 添加标记 | `{ "name": "测试点", "type": "manual", "metadata": {} }` |
| `sfx.play` | 播放音效 | `{ "sfxId": "beep", "volume": 1.0 }` |
| `preset.load` | 加载预设 | `{ "presetId": "pre_001" }` |
| `system.pong` | 心跳回复 | `{}` |

---

## 5. TypeScript 类型定义

> **文件位置**：`control-ui/src/types/api.ts`  
> 必须与本文档保持同步更新

```typescript
// ============ 通用信封 ============
interface MessageEnvelope<T = unknown> {
  type: 'request' | 'response' | 'event';
  id: string;                              // UUID v4
  action: string;
  payload: T;
  timestamp: string;                       // ISO 8601 + TZ
  error?: ApiError;
}

interface ApiError {
  code: string;
  message: string;
}

// ============ 录制 ============
interface RecStartResponse {
  recordingId: string;
  startTime: string;
  outputDir: string;
}

interface RecStopResponse {
  recordingId: string;
  duration: number;          // 秒
  files: RecFile[];
}

interface RecFile {
  sourceId: string;
  path: string;
  size: number;              // 字节
}

type RecordingState = 'idle' | 'recording' | 'paused';

// ============ 场景 ============
interface SceneListItem {
  name: string;
  sources: string[];
}

interface SceneListResponse {
  currentScene: string;
  scenes: SceneListItem[];
}

// ============ 输入源 ============
interface SourceInfo {
  id: string;
  name: string;
  type: string;
  active: boolean;
  resolution: { width: number; height: number };
  fps: number;
  audioLevel: number;        // dB
}

interface SourceListResponse {
  sources: SourceInfo[];
}

// ============ 标记 ============
interface Marker {
  id: string;
  name: string;
  type: string;              // 'manual' | 'auto' | 'scoring'
  timecode: string;          // HH:MM:SS:FF
  timestamp: string;
  metadata?: Record<string, unknown>;
}

// ============ 系统 ============
interface SystemStatus {
  pluginVersion: string;
  obsVersion: string;
  recordingState: RecordingState;
  currentScene: string;
  timecode: string;
  fps: number;
  diskFreeBytes: number;
  cpuUsage: number;
  memoryUsageMB: number;
}

// ============ 项目 ============
interface Project {
  id: string;
  name: string;
  status: 'draft' | 'recording' | 'completed';
  fps: number;
  resolutionX: number;
  resolutionY: number;
  brandMaskEnabled: boolean;
  brandMaskRule: string;
  brandMaskChar: string;
  createdAt: string;
  updatedAt: string;
}

interface ProjectListResponse { projects: Project[]; }

// ============ 产品 ============
interface Product {
  id: string;
  projectId: string;
  sortOrder: number;
  brand: string;
  model: string;
  price: string;
  priceValue: number;
  color: string;
  colorHex: string;
  spec: string;
  specJson: string;
  imagePath: string;
  notes: string;
  createdAt: string;
}

interface ProductListResponse { products: Product[]; }
interface ProductReorderRequest { projectId: string; ids: string[]; }

// ============ 评分维度 ============
interface DimensionTemplate {
  id: string;
  name: string;
  isBuiltin: boolean;
  createdAt: string;
}

interface DimensionItem {
  id?: string;
  dimKey: string;
  label: string;
  weight: number;
  maxScore: number;
  sortOrder: number;
}

// ============ 评分会话 ============
interface ScoringSession {
  id: string;
  projectId: string;
  recordingId?: string;
  judgeName: string;
  sessionName: string;
  startedAt: string;
  completedAt?: string;
  status: 'in_progress' | 'completed';
}

interface ScoreEntry {
  id: string;
  sessionId: string;
  productId: string;
  dimKey: string;
  score: number;
  maxScore: number;
  note: string;
  createdAt: string;
}

interface LeaderboardEntry {
  productId: string;
  brand: string;
  model: string;
  totalScore: number;
  maxPossible: number;
  dimensions: Array<{ dimKey: string; label: string; score: number; maxScore: number }>;
}

interface LeaderboardResponse { leaderboard: LeaderboardEntry[]; }

// ============ WebSocket 事件 ============
type WsEventAction =
  | 'recording.started'
  | 'recording.stopped'
  | 'recording.paused'
  | 'recording.resumed'
  | 'timecode.tick'
  | 'source.status'
  | 'source.added'
  | 'source.removed'
  | 'marker.added'
  | 'scene.changed'
  | 'audio.level'
  | 'system.status'
  | 'system.heartbeat'
  | 'system.error';

type WsCommandAction =
  | 'rec.start'
  | 'rec.stop'
  | 'rec.pause'
  | 'rec.resume'
  | 'scene.switch'
  | 'source.show'
  | 'source.hide'
  | 'marker.add'
  | 'sfx.play'
  | 'preset.load'
  | 'system.pong';
```

---

### 3.7 项目管理

#### `GET /api/projects`
获取项目列表

**响应 200**：
```json
{
  "projects": [
    { "id": "uuid", "name": "2026春季手机横评", "status": "draft", "fps": 60,
      "resolutionX": 1920, "resolutionY": 1080, "brandMaskEnabled": true,
      "createdAt": "2026-05-13T12:00:00.000+08:00" }
  ]
}
```

#### `POST /api/projects`
创建项目

```json
{ "name": "新评测项目", "fps": 60, "resolutionX": 1920, "resolutionY": 1080 }
```

**响应 200**：返回完整项目对象

#### `PUT /api/projects/{id}`
更新项目

#### `DELETE /api/projects/{id}`
删除项目

---

### 3.8 产品管理

#### `GET /api/products?projectId=uuid`
获取产品列表（按 sortOrder 排序）

**响应 200**：
```json
{
  "products": [
    {
      "id": "uuid", "projectId": "uuid", "sortOrder": 0,
      "brand": "华为", "model": "Mate 70 Pro",
      "price": "¥6,999", "priceValue": 6999,
      "color": "雅丹黑", "colorHex": "#1a1a2e",
      "spec": "芯片: 9000S",
      "specJson": "[{\"key\":\"芯片\",\"val\":\"9000S\"}]",
      "imagePath": "C:\\Products\\mate70.png",
      "notes": "", "createdAt": "2026-05-13T12:00:00.000+08:00"
    }
  ]
}
```

#### `POST /api/products`
创建产品

```json
{
  "projectId": "uuid",
  "brand": "华为", "model": "Mate 70 Pro",
  "price": "¥6,999", "priceValue": 6999,
  "color": "雅丹黑", "colorHex": "#1a1a2e",
  "specJson": "[{\"key\":\"芯片\",\"val\":\"9000S\"}]",
  "imagePath": "", "notes": ""
}
```

#### `PUT /api/products/{id}`
更新产品

#### `DELETE /api/products/{id}`
删除产品

#### `PUT /api/products/reorder`
产品排序

```json
{ "projectId": "uuid", "ids": ["id3", "id1", "id2"] }
```

---

### 3.9 评分维度

#### `GET /api/dimensions/templates`
获取维度模板列表

#### `POST /api/dimensions/templates`
创建维度模板

```json
{ "name": "手机评测模板", "items": [
  { "dimKey": "display", "label": "屏幕", "weight": 1.0, "maxScore": 10 },
  { "dimKey": "camera", "label": "相机", "weight": 1.2, "maxScore": 10 }
]}
```

#### `DELETE /api/dimensions/templates/{id}`
删除模板

#### `PUT /api/products/{productId}/dimension-template`
绑定产品的维度模板

```json
{ "templateId": "uuid" }
```

#### `GET /api/products/{productId}/dimension-template`
获取产品绑定的维度模板

---

### 3.10 评分会话

#### `POST /api/scoring/sessions`
创建评分会话

```json
{ "projectId": "uuid", "judgeName": "评委1", "sessionName": "第一轮打分" }
```

#### `GET /api/scoring/sessions/{id}`
获取评分会话详情

#### `POST /api/scoring/sessions/{id}/complete`
完成评分会话

#### `POST /api/scoring/scores`
提交单条评分

```json
{
  "sessionId": "uuid", "productId": "uuid",
  "dimKey": "display", "score": 8.5, "note": "色彩准确"
}
```

#### `GET /api/scoring/scores?sessionId=uuid&productId=uuid`
获取某产品在某会话中的所有评分

#### `GET /api/scoring/leaderboard?sessionId=uuid`
获取排行榜（按总分降序）

**响应 200**：
```json
{
  "leaderboard": [
    {
      "productId": "uuid", "brand": "华为", "model": "Mate 70 Pro",
      "totalScore": 42.5, "maxPossible": 50,
      "dimensions": [
        { "dimKey": "display", "label": "屏幕", "score": 8.5, "maxScore": 10 },
        { "dimKey": "camera", "label": "相机", "score": 9.0, "maxScore": 10 }
      ]
    }
  ]
}
```

---

## 6. 错误码

| 错误码 | HTTP | 含义 |
|--------|------|------|
| `ERR_REC_ALREADY_ACTIVE` | 409 | 录制已在进行中 |
| `ERR_REC_NOT_ACTIVE` | 409 | 未在录制 |
| `ERR_SOURCE_NOT_FOUND` | 400 | 指定的输入源不存在 |
| `ERR_SOURCE_ALREADY_ACTIVE` | 409 | 输入源已激活 |
| `ERR_SCENE_NOT_FOUND` | 400 | 场景不存在 |
| `ERR_PRESET_NOT_FOUND` | 400 | 预设不存在 |
| `ERR_PRESET_NAME_CONFLICT` | 409 | 预设名称冲突 |
| `ERR_INTERNAL` | 500 | 内部错误 |
| `ERR_INVALID_PARAMS` | 400 | 参数无效 |
| `ERR_PROJECT_NOT_FOUND` | 400 | 项目不存在 |
| `ERR_PRODUCT_NOT_FOUND` | 400 | 产品不存在 |
| `ERR_TEMPLATE_NOT_FOUND` | 400 | 维度模板不存在 |
| `ERR_SESSION_NOT_FOUND` | 400 | 评分会话不存在 |
| `ERR_SESSION_COMPLETED` | 409 | 评分会话已结束 |

---

> **记住**：本文档的任何修改必须同步更新：
> 1. ✅ `control-ui/src/types/api.ts`（TypeScript 类型）
> 2. ✅ 对应的 C++ API handler
> 3. ✅ 本文件版本号
