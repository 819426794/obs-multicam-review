# 调试指南 — obs-multicam-review

> **版本**: v1.0 | 适用：开发期调试 / 问题排查

---

## 目录

1. [开发环境调试](#1-开发环境调试)
2. [OBS 插件调试](#2-obs-插件调试)
3. [前端调试](#3-前端调试)
4. [API 调试](#4-api-调试)
5. [常见问题排查](#5-常见问题排查)

---

## 1. 开发环境调试

### 1.1 构建

```powershell
# 构建插件
.\scripts\build.ps1

# 构建前端（开发模式）
cd control-ui && npm run dev

# 构建前端（生产模式）
cd control-ui && npm run build
```

### 1.2 安装到 OBS

```powershell
.\scripts\install.ps1

# 手动安装（调试用）
copy plugin\build\obs-multicam-review.dll "C:\Program Files\obs-studio\obs-plugins\64bit\"
copy plugin\data\* "C:\Program Files\obs-studio\data\obs-plugins\obs-multicam-review\" -Recurse
```

---

## 2. OBS 插件调试

### 2.1 日志查看

OBS 日志文件位置：
```
%APPDATA%\obs-studio\logs\YYYY-MM-DD HH-MM-SS.txt
```

插件日志前缀统一为 `[multicam]`，过滤命令：
```powershell
Select-String -Path "$env:APPDATA\obs-studio\logs\*.txt" -Pattern "\[multicam\]" | Select-Object -Last 50
```

### 2.2 附加调试器

```
1. 打开 Visual Studio 2022
2. Debug → Attach to Process → 选择 obs64.exe
3. 设置断点
4. 加载插件 → 触发断点
```

> **注意**：确保编译 Debug 配置（`cmake --build build --config Debug`），否则断点可能不命中。

### 2.3 日志级别

```cpp
// 编译时控制
#ifdef _DEBUG
  #define LOG_LEVEL  LOG_DEBUG
#else
  #define LOG_LEVEL  LOG_INFO
#endif
```

### 2.4 崩溃分析

```powershell
# 检查 OBS 崩溃日志
Get-Content "$env:APPDATA\obs-studio\crashes\*.txt" -Tail 30

# WinDbg 分析 minidump
windbg -z crash.dmp
```

### 2.5 常见 C++ 陷阱

| 问题 | 症状 | 检查 |
|------|------|------|
| 引用计数泄漏 | OBS 内存增长 | 每个 `addref` 是否对应 `release` |
| 线程竞争 | 间歇性崩溃 | 共享数据是否加锁 |
| 空指针 | OBS 启动即崩溃 | 模块初始化是否检查 `nullptr` |
| 回调阻塞 | 界面卡顿 | 图形线程回调是否耗时<1ms |

---

## 3. 前端调试

### 3.1 开发服务器

```powershell
cd control-ui
npm run dev         # 启动 Vite 开发服务器，默认 http://localhost:5173
```

### 3.2 浏览器 DevTools

| 面板 | 用途 |
|------|------|
| Console | 查看前端日志（`logger.info/warn/error`） |
| Network | 检查 REST API 请求/响应 |
| Application → WebSocket | 检查 WS 消息（Chrome DevTools WS inspector） |
| React DevTools | 组件树 & state 检查 |

### 3.3 WebSocket 调试

```javascript
// 浏览器 Console 中执行
const ws = new WebSocket('ws://localhost:9527/ws');
ws.onmessage = (e) => console.log('WS ←', JSON.parse(e.data));
ws.onopen = () => ws.send(JSON.stringify({ type:'request', id:'test', action:'system.status' }));
```

### 3.4 前端日志规范

```typescript
// utils/logger.ts
const LOG_LEVELS = { debug: 0, info: 1, warn: 2, error: 3 } as const;
const CURRENT_LEVEL = import.meta.env.DEV ? 'debug' : 'info';

export const logger = {
  debug: (msg: string, data?: unknown) => { if (shouldLog('debug')) console.debug(`[debug] ${msg}`, data); },
  info:  (msg: string, data?: unknown) => { if (shouldLog('info'))  console.info(`[info] ${msg}`, data); },
  warn:  (msg: string, data?: unknown) => { if (shouldLog('warn'))  console.warn(`[warn] ${msg}`, data); },
  error: (msg: string, data?: unknown) => { console.error(`[error] ${msg}`, data); },
};
```

---

## 4. API 调试

### 4.1 REST API 测试

```powershell
# 健康检查
curl http://localhost:9527/api/system/health

# 获取状态
curl http://localhost:9527/api/system/status

# 启动录制
curl -X POST http://localhost:9527/api/rec/start
```

### 4.2 WebSocket 测试工具

推荐使用 [websocat](https://github.com/vi/websocat) 或浏览器 Console：

```powershell
# 方式1：websocat
websocat ws://localhost:9527/ws

# 方式2：直接在浏览器 Network → WS 面板查看消息
```

### 4.3 API 调试清单

| 检查项 | 命令/操作 |
|--------|----------|
| 插件是否加载 | 检查 OBS 日志 `[multicam] loaded` |
| 端口是否监听 | `netstat -ano \| findstr 9527` |
| REST 是否响应 | `curl localhost:9527/api/system/health` |
| WS 是否可连接 | 浏览器 Console 测试 WS 连接 |
| API 类型对齐 | 对比 `docs/API_SPEC.md` 与实际响应 |

---

## 5. 常见问题排查

### 5.1 插件未加载

```
1. 检查 dll 是否在正确目录：C:\Program Files\obs-studio\obs-plugins\64bit\
2. 检查依赖：用 Dependency Walker 或 `dumpbin /dependents obs-multicam-review.dll`
3. 查看 OBS 日志：%APPDATA%\obs-studio\logs\ 最新文件
```

### 5.2 端口被占用

```powershell
# 查看 9527 端口
netstat -ano | findstr :9527

# 如果被占用，终止进程
taskkill /PID <PID> /F
```

### 5.3 前端无法连接插件

```
1. 确认 OBS 已启动且插件已加载
2. 检查防火墙：允许 obs64.exe 通过
3. 检查 CORS：确保插件 Web 服务器设置了正确的 CORS 头
4. 在 DevTools Network 标签查看请求状态
```

### 5.4 录制文件为空/损坏

```
1. 检查源是否激活（obs_source_active）
2. 检查编码器配置
3. 查看 OBS 日志中的编码器错误
4. 确保输出目录存在且可写
```

---

## 📝 调试检查清单（上线前）

- [ ] `obs_module_load` 返回 `true`
- [ ] `/api/system/health` 返回 `{"status":"ok"}`
- [ ] WebSocket 可连接，心跳正常
- [ ] 录制启停功能正常
- [ ] 场景切换无内存泄漏
- [ ] 前端连接状态指示正常
- [ ] 语言文件加载正确
- [ ] 关机时插件正常卸载（`obs_module_unload` 执行）
