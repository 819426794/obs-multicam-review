# 数据库 Schema — obs-multicam-review

> **版本**: v0.1.0 | **引擎**: SQLite 3.35+ | **ORM**: 原生 C API  
> ⚠️ 所有表结构变更必须同步更新本文档和对应的 `database.cpp`

---

## Phase 1 实现范围

| 表 | Phase 1 | 说明 |
|------|---------|------|
| `project` | ✅ 完整 | 项目管理 |
| `product` | ✅ 完整 | 产品 CRUD |
| `dimension_template` | ✅ 完整 | 评分维度模板 |
| `dimension_item` | ✅ 完整 | 维度项 |
| `product_dimension_binding` | ✅ 完整 | 产品-模板绑定 |
| `score` | ✅ 完整 | 评分记录 |
| `scoring_session` | ✅ 完整 | 评分会话 |
| 其余表 | Phase 2+ | 按开发路线图增量添加 |

---

## 1. SQLite 集成方式

```cpp
// 使用 sqlite3.c 单文件合并版 (amalgamation)
// 文件位置: plugin/third_party/sqlite3/sqlite3.h + sqlite3.c
// 下载: https://sqlite.org/2024/sqlite-amalgamation-3450000.zip
// 编译: 直接 Include，无需额外链接

// database.h
#include "sqlite3.h"

class Database {
public:
    bool open(const char *db_path);
    void close();
    bool init_schema();  // 自动建表（IF NOT EXISTS）

    // Phase 1 CRUD 接口 (见第 3 节)

private:
    sqlite3 *db_ = nullptr;
    std::mutex mutex_;   // 所有公共方法加锁
};
```

---

## 2. 表结构 (Phase 1)

### 2.1 project — 项目

```sql
CREATE TABLE IF NOT EXISTS project (
    id              TEXT PRIMARY KEY,           -- UUID
    name            TEXT NOT NULL,              -- "2026春季手机横评"
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    fps             INTEGER DEFAULT 60,
    resolution_x    INTEGER DEFAULT 1920,
    resolution_y    INTEGER DEFAULT 1080,
    brand_mask_enabled  INTEGER DEFAULT 1,
    brand_mask_rule     TEXT DEFAULT 'first_char',  -- 'first_char'|'first_hanzi'|'regex'|'all'
    brand_mask_char     TEXT DEFAULT '?',
    status          TEXT DEFAULT 'draft'         -- 'draft'|'recording'|'completed'
);
```

### 2.2 product — 产品

```sql
CREATE TABLE IF NOT EXISTS product (
    id              TEXT PRIMARY KEY,           -- UUID
    project_id      TEXT NOT NULL REFERENCES project(id) ON DELETE CASCADE,
    sort_order      INTEGER NOT NULL DEFAULT 0,
    brand           TEXT NOT NULL,
    model           TEXT NOT NULL,
    price           TEXT,                       -- "¥14,999"
    price_value     REAL,                       -- 14999.0 用于排序
    color           TEXT,                       -- "深空灰"
    color_hex       TEXT,                       -- "#333333" 叠加层取色
    spec            TEXT,                       -- 自由文本规格
    spec_json       TEXT,                       -- JSON [{"key":"像素","val":"3300万"}]
    image_path      TEXT,                       -- 本地路径
    notes           TEXT,                       -- 评测备注
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX idx_product_project ON product(project_id);
```

### 2.3 dimension_template — 评分维度模板

```sql
CREATE TABLE IF NOT EXISTS dimension_template (
    id              TEXT PRIMARY KEY,
    name            TEXT NOT NULL,              -- "手机评测模板"
    is_builtin      INTEGER DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);
```

### 2.4 dimension_item — 维度项

```sql
CREATE TABLE IF NOT EXISTS dimension_item (
    id              TEXT PRIMARY KEY,
    template_id     TEXT NOT NULL REFERENCES dimension_template(id) ON DELETE CASCADE,
    dim_key         TEXT NOT NULL,              -- "display"
    label           TEXT NOT NULL,              -- "屏幕"
    weight          REAL DEFAULT 1.0,
    max_score       INTEGER DEFAULT 10,
    sort_order      INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_dim_item_template ON dimension_item(template_id);
```

### 2.5 product_dimension_binding — 产品-维度绑定

```sql
CREATE TABLE IF NOT EXISTS product_dimension_binding (
    id              TEXT PRIMARY KEY,
    product_id      TEXT NOT NULL REFERENCES product(id) ON DELETE CASCADE,
    template_id     TEXT NOT NULL REFERENCES dimension_template(id),
    UNIQUE(product_id, template_id)
);

CREATE INDEX idx_pdb_product ON product_dimension_binding(product_id);
```

### 2.6 scoring_session — 评分会话

```sql
CREATE TABLE IF NOT EXISTS scoring_session (
    id              TEXT PRIMARY KEY,
    project_id      TEXT NOT NULL REFERENCES project(id),
    recording_id    TEXT,                       -- 关联录制会话
    judge_name      TEXT DEFAULT '评委1',        -- 多评委支持
    session_name    TEXT,                       -- "第一轮打分"
    started_at      TEXT NOT NULL DEFAULT (datetime('now')),
    completed_at    TEXT,
    status          TEXT DEFAULT 'in_progress'   -- 'in_progress'|'completed'
);

CREATE INDEX idx_ss_project ON scoring_session(project_id);
```

### 2.7 score — 评分记录

```sql
CREATE TABLE IF NOT EXISTS score (
    id              TEXT PRIMARY KEY,
    session_id      TEXT NOT NULL REFERENCES scoring_session(id) ON DELETE CASCADE,
    product_id      TEXT NOT NULL REFERENCES product(id),
    dim_key         TEXT NOT NULL,              -- "display"
    score           REAL NOT NULL,              -- 实际得分 (0 ~ max_score)
    max_score       INTEGER DEFAULT 10,
    note            TEXT,                        -- 评分备注
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX idx_score_session ON score(session_id);
CREATE INDEX idx_score_product ON score(product_id);
```

---

## 3. CRUD 接口签名 (Database 类)

```cpp
// ============ Project ============
bool    project_create(const char *json_in, char **json_out);
bool    project_get(const char *id, char **json_out);
bool    project_list(char **json_out);  // JSON 数组
bool    project_update(const char *id, const char *json_in);
bool    project_delete(const char *id);

// ============ Product ============
bool    product_create(const char *project_id, const char *json_in, char **json_out);
bool    product_get(const char *id, char **json_out);
bool    product_list(const char *project_id, char **json_out);
bool    product_update(const char *id, const char *json_in);
bool    product_delete(const char *id);
bool    product_reorder(const char *project_id, const char *json_ids);  // JSON 数组

// ============ Dimension Template ============
bool    dim_template_create(const char *json_in, char **json_out);
bool    dim_template_get(const char *id, char **json_out);
bool    dim_template_list(char **json_out);
bool    dim_template_delete(const char *id);
bool    dim_item_create(const char *template_id, const char *json_in);
bool    dim_item_list(const char *template_id, char **json_out);

// ============ Product-Dimension Binding ============
bool    binding_set(const char *product_id, const char *template_id);
bool    binding_get(const char *product_id, char **json_out);  // 返回 template 详情

// ============ Scoring ============
bool    scoring_session_create(const char *json_in, char **json_out);
bool    scoring_session_get(const char *id, char **json_out);
bool    scoring_session_complete(const char *id);
bool    score_submit(const char *session_id, const char *product_id,
                     const char *dim_key, double score, const char *note);
bool    score_get_by_product(const char *session_id, const char *product_id, char **json_out);
bool    score_get_leaderboard(const char *session_id, char **json_out);
```

---

## 4. JSON 数据格式

### Product (JSON 输入/输出)

```json
{
  "id": "uuid",
  "projectId": "uuid",
  "sortOrder": 0,
  "brand": "华为",
  "model": "Mate 70 Pro",
  "price": "¥6,999",
  "priceValue": 6999,
  "color": "雅丹黑",
  "colorHex": "#1a1a2e",
  "spec": "9000S芯片",
  "specJson": "[{\"key\":\"芯片\",\"val\":\"9000S\"}]",
  "imagePath": "C:\\Products\\mate70.png",
  "notes": "本次横评旗舰组",
  "createdAt": "2026-05-13T12:00:00.000+08:00"
}
```

### Score (JSON 输出)

```json
{
  "id": "uuid",
  "sessionId": "uuid",
  "productId": "uuid",
  "dimKey": "display",
  "score": 8.5,
  "maxScore": 10,
  "note": "色彩准确",
  "createdAt": "2026-05-13T12:30:00.000+08:00"
}
```

---

## 5. 数据迁移策略

| 版本 | 变更 | SQL |
|------|------|-----|
| v0.1.0 | 初始创建 | `CREATE TABLE IF NOT EXISTS ...` |
| v0.2.0+ | 后续变更 | `ALTER TABLE ... ADD COLUMN` |

> **规则**：所有 CREATE TABLE 必须用 `IF NOT EXISTS`；修改字段用 `ALTER TABLE ADD COLUMN`（禁止 DROP/RENAME 旧字段，防止数据丢失）

---

## 6. 调试 SQL

数据库文件位置：
```
%APPDATA%\obs-studio\plugin_data\obs-multicam-review\multicam.db
```

测试查询：
```powershell
sqlite3.exe "$env:APPDATA\obs-studio\plugin_data\obs-multicam-review\multicam.db" ".tables"
sqlite3.exe "$env:APPDATA\obs-studio\plugin_data\obs-multicam-review\multicam.db" "SELECT * FROM product;"
```
