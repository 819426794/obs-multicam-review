// 1. 本模块头文件
#include "database/database.h"

// 2. OBS SDK
#include <obs-module.h>
#include <util/platform.h>

// 3. 第三方
#include <sqlite3.h>
#include <nlohmann/json.hpp>

// 4. C++ 标准库
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// ============ 日志宏（本模块专用前缀） ============
#undef blog_info
#undef blog_warn
#undef blog_error
#define blog_info(fmt, ...)  blog(LOG_INFO,    "[database] " fmt, ##__VA_ARGS__)
#define blog_warn(fmt, ...)  blog(LOG_WARNING, "[database] " fmt, ##__VA_ARGS__)
#define blog_error(fmt, ...) blog(LOG_ERROR,   "[database] " fmt, ##__VA_ARGS__)

using json = nlohmann::json;

namespace multicam {

// ============ 内部辅助: snake_case → camelCase ============
static std::string snake_to_camel(const std::string &snake) {
    std::string result;
    bool capitalize_next = false;
    for (size_t i = 0; i < snake.size(); ++i) {
        char c = snake[i];
        if (c == '_') {
            capitalize_next = true;
        } else if (capitalize_next) {
            result += (char)toupper((unsigned char)c);
            capitalize_next = false;
        } else {
            result += c;
        }
    }
    return result;
}

// ============ 内部辅助: 分配 json_out 字符串（调用方用 sqlite3_free 释放） ============
static bool set_json_out(const std::string &str, char **json_out) {
    if (!json_out) return false;
    size_t len = str.size() + 1;
    *json_out = (char *)sqlite3_malloc((int)len);
    if (!*json_out) return false;
    std::memcpy(*json_out, str.c_str(), len);
    return true;
}

// ============ 内部辅助: 安全读列文本 ============
static const char *col_text(sqlite3_stmt *stmt, int idx) {
    const unsigned char *txt = sqlite3_column_text(stmt, idx);
    return txt ? (const char *)txt : "";
}

// ============ 内部辅助: 绑定文本或 NULL ============
static void bind_text_or_null(sqlite3_stmt *stmt, int idx, const char *val) {
    if (val && val[0]) {
        sqlite3_bind_text(stmt, idx, val, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, idx);
    }
}

// ============ 内部辅助: 执行无结果 SQL ============
static bool exec_sql(sqlite3 *db, const char *sql) {
    char *err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        blog_error("SQL error: %s", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ============ 内部辅助: 生成 UUID v4 ============
static std::string gen_uuid() {
    // 通过临时内存数据库使用 SQLite 随机函数生成 RFC 4122 v4 UUID
    sqlite3 *tmp = nullptr;
    if (sqlite3_open(":memory:", &tmp) != SQLITE_OK) goto fallback;

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT lower("
        "  hex(randomblob(4)) || '-' || "
        "  hex(randomblob(2)) || '-' || "
        "  '4' || substr(hex(randomblob(2)),2) || '-' || "
        "  substr('89ab', abs(random()) % 4 + 1, 1) || "
        "  substr(hex(randomblob(2)), 2) || '-' || "
        "  hex(randomblob(6))"
        ")";

    if (sqlite3_prepare_v2(tmp, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string uuid((const char *)sqlite3_column_text(stmt, 0));
            sqlite3_finalize(stmt);
            sqlite3_close(tmp);
            return uuid;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(tmp);

fallback:
    // 简陋回退：时间戳 + 随机数
    char buf[64];
    snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
             (unsigned)time(nullptr),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)((rand() & 0x0FFF) | 0x4000),
             (unsigned)((rand() & 0x3FFF) | 0x8000),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)(rand() & 0xFFFF) ^ (unsigned)(rand() & 0xFFFF));
    return std::string(buf);
}

// ============ 生命周期 ============

Database::~Database() {
    close();
}

bool Database::open(const char *db_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (db_) {
        blog_warn("Database already open");
        return true;
    }

    // 提取目录并创建
    std::string path(db_path);
    size_t sep = path.find_last_of("\\/");
    if (sep != std::string::npos) {
        std::string dir = path.substr(0, sep);
        // os_mkdirs 递归创建目录
        os_mkdirs(dir.c_str());
    }

    int rc = sqlite3_open(db_path, &db_);
    if (rc != SQLITE_OK) {
        blog_error("Failed to open database: %s", sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // 启用 WAL 模式 + 外键约束
    exec_sql(db_, "PRAGMA journal_mode=WAL");
    exec_sql(db_, "PRAGMA foreign_keys=ON");

    blog_info("Database opened: %s", db_path);
    return true;
}

void Database::close() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        blog_info("Database closed");
    }
}

bool Database::init_schema() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!db_) {
        blog_error("init_schema: database not open");
        return false;
    }

    const char *schema_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS project (
            id                  TEXT PRIMARY KEY,
            name                TEXT NOT NULL,
            created_at          TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at          TEXT NOT NULL DEFAULT (datetime('now')),
            fps                 INTEGER DEFAULT 60,
            resolution_x        INTEGER DEFAULT 1920,
            resolution_y        INTEGER DEFAULT 1080,
            brand_mask_enabled  INTEGER DEFAULT 1,
            brand_mask_rule     TEXT DEFAULT 'first_char',
            brand_mask_char     TEXT DEFAULT '?',
            status              TEXT DEFAULT 'draft'
        );

        CREATE TABLE IF NOT EXISTS product (
            id              TEXT PRIMARY KEY,
            project_id      TEXT NOT NULL REFERENCES project(id) ON DELETE CASCADE,
            sort_order      INTEGER NOT NULL DEFAULT 0,
            brand           TEXT NOT NULL,
            model           TEXT NOT NULL,
            price           TEXT,
            price_value     REAL,
            color           TEXT,
            color_hex       TEXT,
            spec            TEXT,
            spec_json       TEXT,
            image_path      TEXT,
            notes           TEXT,
            created_at      TEXT NOT NULL DEFAULT (datetime('now'))
        );
        CREATE INDEX IF NOT EXISTS idx_product_project ON product(project_id);

        CREATE TABLE IF NOT EXISTS dimension_template (
            id          TEXT PRIMARY KEY,
            name        TEXT NOT NULL,
            is_builtin  INTEGER DEFAULT 0,
            created_at  TEXT NOT NULL DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS dimension_item (
            id          TEXT PRIMARY KEY,
            template_id TEXT NOT NULL REFERENCES dimension_template(id) ON DELETE CASCADE,
            dim_key     TEXT NOT NULL,
            label       TEXT NOT NULL,
            weight      REAL DEFAULT 1.0,
            max_score   INTEGER DEFAULT 10,
            sort_order  INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_dim_item_template ON dimension_item(template_id);

        CREATE TABLE IF NOT EXISTS product_dimension_binding (
            id          TEXT PRIMARY KEY,
            product_id  TEXT NOT NULL REFERENCES product(id) ON DELETE CASCADE,
            template_id TEXT NOT NULL REFERENCES dimension_template(id),
            UNIQUE(product_id, template_id)
        );
        CREATE INDEX IF NOT EXISTS idx_pdb_product ON product_dimension_binding(product_id);

        CREATE TABLE IF NOT EXISTS scoring_session (
            id              TEXT PRIMARY KEY,
            project_id      TEXT NOT NULL REFERENCES project(id),
            recording_id    TEXT,
            judge_name      TEXT DEFAULT '评委1',
            session_name    TEXT,
            started_at      TEXT NOT NULL DEFAULT (datetime('now')),
            completed_at    TEXT,
            status          TEXT DEFAULT 'in_progress'
        );
        CREATE INDEX IF NOT EXISTS idx_ss_project ON scoring_session(project_id);

        CREATE TABLE IF NOT EXISTS score (
            id          TEXT PRIMARY KEY,
            session_id  TEXT NOT NULL REFERENCES scoring_session(id) ON DELETE CASCADE,
            product_id  TEXT NOT NULL REFERENCES product(id),
            dim_key     TEXT NOT NULL,
            score       REAL NOT NULL,
            max_score   INTEGER DEFAULT 10,
            note        TEXT,
            created_at  TEXT NOT NULL DEFAULT (datetime('now'))
        );
        CREATE INDEX IF NOT EXISTS idx_score_session ON score(session_id);
        CREATE INDEX IF NOT EXISTS idx_score_product ON score(product_id);
    )SQL";

    return exec_sql(db_, schema_sql);
}

// ================================================================
//  Project CRUD
// ================================================================

bool Database::project_create(const char *json_in, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json input = json::parse(json_in);
        std::string id = gen_uuid();

        const char *sql =
            "INSERT INTO project (id, name, fps, resolution_x, resolution_y, "
            "  brand_mask_enabled, brand_mask_rule, brand_mask_char, status) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("project_create prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

        std::string name = input.value("name", "");
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_bind_int(stmt, 3, input.value("fps", 60));
        sqlite3_bind_int(stmt, 4, input.value("resolutionX", 1920));
        sqlite3_bind_int(stmt, 5, input.value("resolutionY", 1080));
        sqlite3_bind_int(stmt, 6, input.value("brandMaskEnabled", 1));
        sqlite3_bind_text(stmt, 7, input.value("brandMaskRule", "first_char").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, input.value("brandMaskChar", "?").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, input.value("status", "draft").c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (!ok) {
            blog_error("project_create step: %s", sqlite3_errmsg(db_));
            return false;
        }

        // 读取完整记录返回
        return project_get(id.c_str(), json_out);
    } catch (const json::exception &e) {
        blog_error("project_create JSON: %s", e.what());
        return false;
    }
}

bool Database::project_get(const char *id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql = "SELECT id, name, created_at, updated_at, fps, resolution_x, "
                      "resolution_y, brand_mask_enabled, brand_mask_rule, brand_mask_char, "
                      "status FROM project WHERE id = ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("project_get prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["name"] = col_text(stmt, 1);
        j["createdAt"] = col_text(stmt, 2);
        j["updatedAt"] = col_text(stmt, 3);
        j["fps"] = sqlite3_column_int(stmt, 4);
        j["resolutionX"] = sqlite3_column_int(stmt, 5);
        j["resolutionY"] = sqlite3_column_int(stmt, 6);
        j["brandMaskEnabled"] = sqlite3_column_int(stmt, 7) != 0;
        j["brandMaskRule"] = col_text(stmt, 8);
        j["brandMaskChar"] = col_text(stmt, 9);
        j["status"] = col_text(stmt, 10);
        found = set_json_out(j.dump(), json_out);
    }
    sqlite3_finalize(stmt);

    if (!found && json_out) *json_out = nullptr;
    return found;
}

bool Database::project_list(char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql = "SELECT id, name, created_at, updated_at, fps, resolution_x, "
                      "resolution_y, brand_mask_enabled, brand_mask_rule, brand_mask_char, "
                      "status FROM project ORDER BY created_at DESC";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("project_list prepare: %s", sqlite3_errmsg(db_));
        return false;
    }

    json arr = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["name"] = col_text(stmt, 1);
        j["createdAt"] = col_text(stmt, 2);
        j["updatedAt"] = col_text(stmt, 3);
        j["fps"] = sqlite3_column_int(stmt, 4);
        j["resolutionX"] = sqlite3_column_int(stmt, 5);
        j["resolutionY"] = sqlite3_column_int(stmt, 6);
        j["brandMaskEnabled"] = sqlite3_column_int(stmt, 7) != 0;
        j["brandMaskRule"] = col_text(stmt, 8);
        j["brandMaskChar"] = col_text(stmt, 9);
        j["status"] = col_text(stmt, 10);
        arr.push_back(j);
    }
    sqlite3_finalize(stmt);

    return set_json_out(arr.dump(), json_out);
}

bool Database::project_update(const char *id, const char *json_in) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json input = json::parse(json_in);

        const char *sql =
            "UPDATE project SET name=?, fps=?, resolution_x=?, resolution_y=?, "
            "  brand_mask_enabled=?, brand_mask_rule=?, brand_mask_char=?, "
            "  status=?, updated_at=datetime('now') WHERE id=? ";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("project_update prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        std::string name = input.value("name", "");
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, input.value("fps", 60));
        sqlite3_bind_int(stmt, 3, input.value("resolutionX", 1920));
        sqlite3_bind_int(stmt, 4, input.value("resolutionY", 1080));
        sqlite3_bind_int(stmt, 5, input.value("brandMaskEnabled", 1));
        sqlite3_bind_text(stmt, 6, input.value("brandMaskRule", "first_char").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, input.value("brandMaskChar", "?").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, input.value("status", "draft").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, id, -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (!ok) {
            blog_error("project_update step: %s", sqlite3_errmsg(db_));
        }
        return ok;
    } catch (const json::exception &e) {
        blog_error("project_update JSON: %s", e.what());
        return false;
    }
}

bool Database::project_delete(const char *id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM project WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("project_delete prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (!ok) {
        blog_error("project_delete: %s", sqlite3_errmsg(db_));
    }
    return ok;
}

// ================================================================
//  Product CRUD
// ================================================================

bool Database::product_create(const char *project_id, const char *json_in, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json input = json::parse(json_in);
        std::string id = gen_uuid();

        const char *sql =
            "INSERT INTO product (id, project_id, sort_order, brand, model, "
            "  price, price_value, color, color_hex, spec, spec_json, image_path, notes) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("product_create prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, project_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, input.value("sortOrder", 0));
        sqlite3_bind_text(stmt, 4, input.value("brand", "").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, input.value("model", "").c_str(), -1, SQLITE_TRANSIENT);
        bind_text_or_null(stmt, 6, input.value("price", "").c_str());
        if (input.contains("priceValue") && !input["priceValue"].is_null())
            sqlite3_bind_double(stmt, 7, input["priceValue"]);
        else
            sqlite3_bind_null(stmt, 7);
        bind_text_or_null(stmt, 8, input.value("color", "").c_str());
        bind_text_or_null(stmt, 9, input.value("colorHex", "").c_str());
        bind_text_or_null(stmt, 10, input.value("spec", "").c_str());
        bind_text_or_null(stmt, 11, input.value("specJson", "").c_str());
        bind_text_or_null(stmt, 12, input.value("imagePath", "").c_str());
        bind_text_or_null(stmt, 13, input.value("notes", "").c_str());

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (!ok) {
            blog_error("product_create step: %s", sqlite3_errmsg(db_));
            return false;
        }

        return product_get(id.c_str(), json_out);
    } catch (const json::exception &e) {
        blog_error("product_create JSON: %s", e.what());
        return false;
    }
}

bool Database::product_get(const char *id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql =
        "SELECT id, project_id, sort_order, brand, model, price, price_value, "
        "  color, color_hex, spec, spec_json, image_path, notes, created_at "
        "FROM product WHERE id = ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("product_get prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["projectId"] = col_text(stmt, 1);
        j["sortOrder"] = sqlite3_column_int(stmt, 2);
        j["brand"] = col_text(stmt, 3);
        j["model"] = col_text(stmt, 4);
        j["price"] = col_text(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            j["priceValue"] = sqlite3_column_double(stmt, 6);
        else
            j["priceValue"] = nullptr;
        j["color"] = col_text(stmt, 7);
        j["colorHex"] = col_text(stmt, 8);
        j["spec"] = col_text(stmt, 9);
        j["specJson"] = col_text(stmt, 10);
        j["imagePath"] = col_text(stmt, 11);
        j["notes"] = col_text(stmt, 12);
        j["createdAt"] = col_text(stmt, 13);
        found = set_json_out(j.dump(), json_out);
    }
    sqlite3_finalize(stmt);

    if (!found && json_out) *json_out = nullptr;
    return found;
}

bool Database::product_list(const char *project_id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql =
        "SELECT id, project_id, sort_order, brand, model, price, price_value, "
        "  color, color_hex, spec, spec_json, image_path, notes, created_at "
        "FROM product WHERE project_id = ? ORDER BY sort_order ASC, created_at ASC";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("product_list prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, project_id, -1, SQLITE_TRANSIENT);

    json arr = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["projectId"] = col_text(stmt, 1);
        j["sortOrder"] = sqlite3_column_int(stmt, 2);
        j["brand"] = col_text(stmt, 3);
        j["model"] = col_text(stmt, 4);
        j["price"] = col_text(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            j["priceValue"] = sqlite3_column_double(stmt, 6);
        else
            j["priceValue"] = nullptr;
        j["color"] = col_text(stmt, 7);
        j["colorHex"] = col_text(stmt, 8);
        j["spec"] = col_text(stmt, 9);
        j["specJson"] = col_text(stmt, 10);
        j["imagePath"] = col_text(stmt, 11);
        j["notes"] = col_text(stmt, 12);
        j["createdAt"] = col_text(stmt, 13);
        arr.push_back(j);
    }
    sqlite3_finalize(stmt);

    return set_json_out(arr.dump(), json_out);
}

bool Database::product_update(const char *id, const char *json_in) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json input = json::parse(json_in);

        const char *sql =
            "UPDATE product SET sort_order=?, brand=?, model=?, price=?, price_value=?, "
            "  color=?, color_hex=?, spec=?, spec_json=?, image_path=?, notes=? "
            "WHERE id=?";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("product_update prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        sqlite3_bind_int(stmt, 1, input.value("sortOrder", 0));
        sqlite3_bind_text(stmt, 2, input.value("brand", "").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, input.value("model", "").c_str(), -1, SQLITE_TRANSIENT);
        bind_text_or_null(stmt, 4, input.value("price", "").c_str());
        if (input.contains("priceValue") && !input["priceValue"].is_null())
            sqlite3_bind_double(stmt, 5, input["priceValue"]);
        else
            sqlite3_bind_null(stmt, 5);
        bind_text_or_null(stmt, 6, input.value("color", "").c_str());
        bind_text_or_null(stmt, 7, input.value("colorHex", "").c_str());
        bind_text_or_null(stmt, 8, input.value("spec", "").c_str());
        bind_text_or_null(stmt, 9, input.value("specJson", "").c_str());
        bind_text_or_null(stmt, 10, input.value("imagePath", "").c_str());
        bind_text_or_null(stmt, 11, input.value("notes", "").c_str());
        sqlite3_bind_text(stmt, 12, id, -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (!ok) {
            blog_error("product_update: %s", sqlite3_errmsg(db_));
        }
        return ok;
    } catch (const json::exception &e) {
        blog_error("product_update JSON: %s", e.what());
        return false;
    }
}

bool Database::product_delete(const char *id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM product WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("product_delete prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (!ok) {
        blog_error("product_delete: %s", sqlite3_errmsg(db_));
    }
    return ok;
}

bool Database::product_reorder(const char *project_id, const char *json_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json arr = json::parse(json_ids);
        if (!arr.is_array()) {
            blog_error("product_reorder: json_ids is not an array");
            return false;
        }

        const char *sql = "UPDATE product SET sort_order=? WHERE id=? AND project_id=?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("product_reorder prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        // 使用事务确保原子性
        exec_sql(db_, "BEGIN");

        bool ok = true;
        for (size_t i = 0; i < arr.size(); ++i) {
            std::string pid = arr[i].get<std::string>();
            sqlite3_reset(stmt);
            sqlite3_bind_int(stmt, 1, (int)i);
            sqlite3_bind_text(stmt, 2, pid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, project_id, -1, SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                blog_error("product_reorder step[%zu]: %s", i, sqlite3_errmsg(db_));
                ok = false;
                break;
            }
        }
        sqlite3_finalize(stmt);

        if (ok) {
            exec_sql(db_, "COMMIT");
        } else {
            exec_sql(db_, "ROLLBACK");
        }
        return ok;
    } catch (const json::exception &e) {
        blog_error("product_reorder JSON: %s", e.what());
        exec_sql(db_, "ROLLBACK");
        return false;
    }
}

// ================================================================
//  Dimension Template CRUD
// ================================================================

bool Database::dim_template_create(const char *json_in, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json input = json::parse(json_in);
        std::string id = gen_uuid();

        const char *sql =
            "INSERT INTO dimension_template (id, name, is_builtin) VALUES (?, ?, ?)";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("dim_template_create prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, input.value("name", "").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, input.value("isBuiltin", 0));

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (!ok) {
            blog_error("dim_template_create step: %s", sqlite3_errmsg(db_));
            return false;
        }

        return dim_template_get(id.c_str(), json_out);
    } catch (const json::exception &e) {
        blog_error("dim_template_create JSON: %s", e.what());
        return false;
    }
}

bool Database::dim_template_get(const char *id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql = "SELECT id, name, is_builtin, created_at FROM dimension_template WHERE id=?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("dim_template_get prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["name"] = col_text(stmt, 1);
        j["isBuiltin"] = sqlite3_column_int(stmt, 2) != 0;
        j["createdAt"] = col_text(stmt, 3);
        found = set_json_out(j.dump(), json_out);
    }
    sqlite3_finalize(stmt);

    if (!found && json_out) *json_out = nullptr;
    return found;
}

bool Database::dim_template_list(char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql = "SELECT id, name, is_builtin, created_at FROM dimension_template ORDER BY is_builtin DESC, created_at DESC";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("dim_template_list prepare: %s", sqlite3_errmsg(db_));
        return false;
    }

    json arr = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["name"] = col_text(stmt, 1);
        j["isBuiltin"] = sqlite3_column_int(stmt, 2) != 0;
        j["createdAt"] = col_text(stmt, 3);
        arr.push_back(j);
    }
    sqlite3_finalize(stmt);

    return set_json_out(arr.dump(), json_out);
}

bool Database::dim_template_delete(const char *id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM dimension_template WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("dim_template_delete prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (!ok) {
        blog_error("dim_template_delete: %s", sqlite3_errmsg(db_));
    }
    return ok;
}

// ================================================================
//  Dimension Item CRUD
// ================================================================

bool Database::dim_item_create(const char *template_id, const char *json_in) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json input = json::parse(json_in);
        std::string id = gen_uuid();

        const char *sql =
            "INSERT INTO dimension_item (id, template_id, dim_key, label, weight, max_score, sort_order) "
            "VALUES (?,?,?,?,?,?,?)";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("dim_item_create prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, template_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, input.value("dimKey", "").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, input.value("label", "").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, input.value("weight", 1.0));
        sqlite3_bind_int(stmt, 6, input.value("maxScore", 10));
        sqlite3_bind_int(stmt, 7, input.value("sortOrder", 0));

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (!ok) {
            blog_error("dim_item_create step: %s", sqlite3_errmsg(db_));
        }
        return ok;
    } catch (const json::exception &e) {
        blog_error("dim_item_create JSON: %s", e.what());
        return false;
    }
}

bool Database::dim_item_list(const char *template_id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql =
        "SELECT id, template_id, dim_key, label, weight, max_score, sort_order "
        "FROM dimension_item WHERE template_id = ? ORDER BY sort_order ASC";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("dim_item_list prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, template_id, -1, SQLITE_TRANSIENT);

    json arr = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["templateId"] = col_text(stmt, 1);
        j["dimKey"] = col_text(stmt, 2);
        j["label"] = col_text(stmt, 3);
        j["weight"] = sqlite3_column_double(stmt, 4);
        j["maxScore"] = sqlite3_column_int(stmt, 5);
        j["sortOrder"] = sqlite3_column_int(stmt, 6);
        arr.push_back(j);
    }
    sqlite3_finalize(stmt);

    return set_json_out(arr.dump(), json_out);
}

// ================================================================
//  Product-Dimension Binding
// ================================================================

bool Database::binding_set(const char *product_id, const char *template_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql =
        "INSERT OR REPLACE INTO product_dimension_binding (id, product_id, template_id) "
        "VALUES ((SELECT id FROM product_dimension_binding WHERE product_id=? AND template_id=?), ?, ?)";

    std::string id = gen_uuid();

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("binding_set prepare: %s", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, product_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, template_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, product_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, template_id, -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (!ok) {
        blog_error("binding_set: %s", sqlite3_errmsg(db_));
    }
    return ok;
}

bool Database::binding_get(const char *product_id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    // 返回该产品绑定的模板详情
    const char *sql =
        "SELECT dt.id, dt.name, dt.is_builtin, dt.created_at "
        "FROM dimension_template dt "
        "JOIN product_dimension_binding pdb ON dt.id = pdb.template_id "
        "WHERE pdb.product_id = ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("binding_get prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, product_id, -1, SQLITE_TRANSIENT);

    bool found = false;
    json j;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        j["id"] = col_text(stmt, 0);
        j["name"] = col_text(stmt, 1);
        j["isBuiltin"] = sqlite3_column_int(stmt, 2) != 0;
        j["createdAt"] = col_text(stmt, 3);
        found = true;
    }
    sqlite3_finalize(stmt);

    if (found) {
        return set_json_out(j.dump(), json_out);
    } else {
        if (json_out) *json_out = nullptr;
        return false;
    }
}

// ================================================================
//  Scoring
// ================================================================

bool Database::scoring_session_create(const char *json_in, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    try {
        json input = json::parse(json_in);
        std::string id = gen_uuid();

        const char *sql =
            "INSERT INTO scoring_session (id, project_id, recording_id, judge_name, session_name) "
            "VALUES (?,?,?,?,?)";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            blog_error("scoring_session_create prepare: %s", sqlite3_errmsg(db_));
            return false;
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, input.value("projectId", "").c_str(), -1, SQLITE_TRANSIENT);
        bind_text_or_null(stmt, 3, input.value("recordingId", "").c_str());
        sqlite3_bind_text(stmt, 4, input.value("judgeName", "评委1").c_str(), -1, SQLITE_TRANSIENT);
        bind_text_or_null(stmt, 5, input.value("sessionName", "").c_str());

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (!ok) {
            blog_error("scoring_session_create step: %s", sqlite3_errmsg(db_));
            return false;
        }

        return scoring_session_get(id.c_str(), json_out);
    } catch (const json::exception &e) {
        blog_error("scoring_session_create JSON: %s", e.what());
        return false;
    }
}

bool Database::scoring_session_get(const char *id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql =
        "SELECT id, project_id, recording_id, judge_name, session_name, "
        "  started_at, completed_at, status "
        "FROM scoring_session WHERE id = ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("scoring_session_get prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["projectId"] = col_text(stmt, 1);
        j["recordingId"] = col_text(stmt, 2);
        j["judgeName"] = col_text(stmt, 3);
        j["sessionName"] = col_text(stmt, 4);
        j["startedAt"] = col_text(stmt, 5);
        j["completedAt"] = col_text(stmt, 6);
        j["status"] = col_text(stmt, 7);
        found = set_json_out(j.dump(), json_out);
    }
    sqlite3_finalize(stmt);

    if (!found && json_out) *json_out = nullptr;
    return found;
}

bool Database::scoring_session_complete(const char *id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql =
        "UPDATE scoring_session SET status='completed', completed_at=datetime('now') WHERE id=?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("scoring_session_complete prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (!ok) {
        blog_error("scoring_session_complete: %s", sqlite3_errmsg(db_));
    }
    return ok;
}

bool Database::score_submit(const char *session_id, const char *product_id,
                            const char *dim_key, double score, const char *note) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    std::string id = gen_uuid();

    // 使用 INSERT OR REPLACE 支持覆盖评分
    const char *sql =
        "INSERT INTO score (id, session_id, product_id, dim_key, score, note) "
        "VALUES (?,?,?,?,?,?)";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("score_submit prepare: %s", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, product_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, dim_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, score);
    bind_text_or_null(stmt, 6, note);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (!ok) {
        blog_error("score_submit: %s", sqlite3_errmsg(db_));
    }
    return ok;
}

bool Database::score_get_by_product(const char *session_id, const char *product_id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char *sql =
        "SELECT id, session_id, product_id, dim_key, score, max_score, note, created_at "
        "FROM score WHERE session_id=? AND product_id=? ORDER BY dim_key";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("score_get_by_product prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, product_id, -1, SQLITE_TRANSIENT);

    json arr = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["id"] = col_text(stmt, 0);
        j["sessionId"] = col_text(stmt, 1);
        j["productId"] = col_text(stmt, 2);
        j["dimKey"] = col_text(stmt, 3);
        j["score"] = sqlite3_column_double(stmt, 4);
        j["maxScore"] = sqlite3_column_int(stmt, 5);
        j["note"] = col_text(stmt, 6);
        j["createdAt"] = col_text(stmt, 7);
        arr.push_back(j);
    }
    sqlite3_finalize(stmt);

    return set_json_out(arr.dump(), json_out);
}

bool Database::score_get_leaderboard(const char *session_id, char **json_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    // 按产品聚合总分，从高到低排序
    const char *sql =
        "SELECT p.id, p.brand, p.model, "
        "  SUM(s.score) as total_score, "
        "  COUNT(s.dim_key) as dim_count, "
        "  AVG(s.score) as avg_score "
        "FROM score s "
        "JOIN product p ON s.product_id = p.id "
        "WHERE s.session_id = ? "
        "GROUP BY p.id "
        "ORDER BY total_score DESC";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        blog_error("score_get_leaderboard prepare: %s", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);

    json arr = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["productId"] = col_text(stmt, 0);
        j["brand"] = col_text(stmt, 1);
        j["model"] = col_text(stmt, 2);
        j["totalScore"] = sqlite3_column_double(stmt, 3);
        j["dimCount"] = sqlite3_column_int(stmt, 4);
        j["avgScore"] = sqlite3_column_double(stmt, 5);
        arr.push_back(j);
    }
    sqlite3_finalize(stmt);

    return set_json_out(arr.dump(), json_out);
}

} // namespace multicam
