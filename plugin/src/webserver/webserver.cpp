#include "webserver.h"
#include "../plugin.h"

// OBS SDK
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

// civetweb (C 库)
extern "C" {
#include "civetweb.h"
}

// nlohmann/json
#include <nlohmann/json.hpp>

// Source & Scene managers
#include "../source/source_manager.h"
#include "../scene/scene_manager.h"

// Audio, Recorder, Timecode
#include "../audio/audio_manager.h"
#include "../recorder/recorder.h"
#include "../timecode/timecode_gen.h"

// C++ 标准库
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <atomic>
#include <ctime>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

// ============ 日志宏 ============
#define blog_info(fmt, ...)  blog(LOG_INFO,    "[webserver] " fmt, ##__VA_ARGS__)
#define blog_warn(fmt, ...)  blog(LOG_WARNING, "[webserver] " fmt, ##__VA_ARGS__)
#define blog_error(fmt, ...) blog(LOG_ERROR,   "[webserver] " fmt, ##__VA_ARGS__)
#define blog_debug(fmt, ...) blog(LOG_DEBUG,   "[webserver] " fmt, ##__VA_ARGS__)

// ============ 常量 ============
static constexpr int kWebSocketHeartbeatSec = 15;
static constexpr int kWebSocketTimeoutSec  = 30;

// ============ WebSocket 会话 ============
struct WsSession {
    struct mg_connection *conn = nullptr;
    PluginContext *plugin_ctx = nullptr;
    time_t last_pong = 0;
    bool active = false;
};

static std::mutex g_ws_mutex;
static std::unordered_map<struct mg_connection *, WsSession> g_ws_sessions;
static std::thread g_heartbeat_thread;
static std::atomic<bool> g_heartbeat_running{false};

// ============ 辅助函数 ============

static time_t get_start_time() {
    static time_t s_start = time(nullptr);
    return time(nullptr) - s_start;
}

static std::string get_iso8601_timestamp() {
    time_t now = time(nullptr);
    struct tm tm_buf;
    gmtime_s(&tm_buf, &now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000+08:00", &tm_buf);
    return std::string(buf);
}

static std::string gen_uuid() {
    // 简单的 UUID v4 生成（Phase 1 可用）
    char buf[37];
    snprintf(buf, sizeof(buf),
             "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
             rand() & 0xffff, rand() & 0xffff,
             rand() & 0xffff,
             (rand() & 0x0fff) | 0x4000,
             (rand() & 0x3fff) | 0x8000,
             rand() & 0xffff, rand() & 0xffff, rand() & 0xffff);
    return std::string(buf);
}

static json build_envelope(const std::string &type, const std::string &action,
                           json payload, const std::string &id = "") {
    json env;
    env["type"] = type;
    env["id"] = id.empty() ? gen_uuid() : id;
    env["action"] = action;
    env["payload"] = payload;
    env["timestamp"] = get_iso8601_timestamp();
    return env;
}

static json build_error_envelope(const std::string &id, const std::string &code,
                                 const std::string &message) {
    json env;
    env["type"] = "response";
    env["id"] = id;
    env["action"] = "";
    env["payload"] = json::object();
    env["timestamp"] = get_iso8601_timestamp();
    env["error"]["code"] = code;
    env["error"]["message"] = message;
    return env;
}

// 发送 CORS 预检响应
static void send_cors_preflight(struct mg_connection *conn) {
    mg_printf(conn,
              "HTTP/1.1 204 No Content\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Content-Length: 0\r\n"
              "\r\n");
}

// 发送 JSON 响应
static void send_json_response(struct mg_connection *conn, int status,
                               const json &body) {
    std::string body_str = body.dump();
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              status,
              (status == 200) ? "OK" :
              (status == 400) ? "Bad Request" :
              (status == 409) ? "Conflict" : "Internal Server Error",
              body_str.size(),
              body_str.c_str());
}

// 读取请求体
static std::string read_request_body(struct mg_connection *conn) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (!ri) return "";

    // 检查 Content-Length
    const char *cl_header = mg_get_header(conn, "Content-Length");
    if (!cl_header) return "";

    int content_length = atoi(cl_header);
    if (content_length <= 0 || content_length > 65536) return "";

    std::vector<char> buf(content_length + 1, 0);
    int bytes_read = mg_read(conn, buf.data(), content_length);
    if (bytes_read <= 0) return "";

    return std::string(buf.data(), bytes_read);
}

// ============ 简洁响应辅助 ============

static void resp_ok(struct mg_connection *conn) {
    mg_response_header_start(conn, 200);
    mg_response_header_add(conn, "Content-Type", "application/json", -1);
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
    mg_response_header_send(conn);
    mg_printf(conn, "{\"ok\":true}");
}

static void resp_json(struct mg_connection *conn, const nlohmann::json &j) {
    std::string body = j.dump();
    mg_response_header_start(conn, 200);
    mg_response_header_add(conn, "Content-Type", "application/json", -1);
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
    mg_response_header_send(conn);
    mg_printf(conn, "%s", body.c_str());
}

static void resp_error(struct mg_connection *conn, int status, const std::string &msg) {
    mg_response_header_start(conn, status);
    mg_response_header_add(conn, "Content-Type", "application/json", -1);
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
    mg_response_header_send(conn);
    mg_printf(conn, "{\"error\":\"%s\"}", msg.c_str());
}

// ============ REST 路由处理器 ============

// GET /api/system/health
static int handle_health(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    json body;
    body["status"] = "ok";
    body["uptime"] = static_cast<int>(get_start_time());

    send_json_response(conn, 200, body);
    return 200;
}

// GET /api/system/status
static int handle_system_status(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    const char *rec_state = "idle";
    if (ctx && ctx->recording_active) {
        rec_state = "recording";
    }

    json body;
    body["pluginVersion"] = PLUGIN_VERSION;
    body["obsVersion"] = "30.2.0";  // Phase 1: mock
    body["recordingState"] = rec_state;
    body["currentScene"] = "main_full";
    body["timecode"] = "00:00:00:00";
    body["fps"] = 60;
    body["diskFreeBytes"] = 262144000000LL;
    body["cpuUsage"] = 12.5;
    body["memoryUsageMB"] = 256;

    send_json_response(conn, 200, body);
    return 200;
}

// POST /api/rec/start
static int handle_rec_start(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (ctx && ctx->recording_active) {
        json err = build_error_envelope("", "ERR_REC_ALREADY_ACTIVE",
                                        "录制已在进行中");
        send_json_response(conn, 409, err);
        return 409;
    }

    json body;
    body["recordingId"] = "rec_20260513_123000";
    body["startTime"] = get_iso8601_timestamp();
    body["outputDir"] = "C:\\Recordings\\2026-05-13\\";

    send_json_response(conn, 200, body);
    return 200;
}

// POST /api/rec/stop
static int handle_rec_stop(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    json body;
    body["recordingId"] = "rec_20260513_123000";
    body["duration"] = 125.5;

    json file1;
    file1["sourceId"] = "cam_main";
    file1["path"] = "C:\\Recordings\\2026-05-13\\cam_main_123000.mp4";
    file1["size"] = 524288000;
    json file2;
    file2["sourceId"] = "cam_top";
    file2["path"] = "C:\\Recordings\\2026-05-13\\cam_top_123000.mp4";
    file2["size"] = 498073600;

    body["files"] = json::array({file1, file2});

    send_json_response(conn, 200, body);
    return 200;
}

// ============ Scene 路由 ============

// GET /api/scene/list
static int handle_scene_list(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->scenes) {
        resp_error(conn, 500, "scene_manager not available");
        return 500;
    }

    resp_json(conn, ctx->scenes->list_json());
    return 200;
}

// POST /api/scene/switch
static int handle_scene_switch(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->scenes) {
        resp_error(conn, 500, "scene_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string scene_name = req.value("sceneName", "");
        if (scene_name.empty()) {
            resp_error(conn, 400, "missing sceneName");
            return 400;
        }
        if (ctx->scenes->switch_to(scene_name)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to switch scene");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/scene/create
static int handle_scene_create(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->scenes) {
        resp_error(conn, 500, "scene_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string scene_name = req.value("sceneName", "");
        std::string layout_type = req.value("layoutType", "fullscreen");
        std::vector<std::string> source_names;
        if (req.contains("sourceNames") && req["sourceNames"].is_array()) {
            for (const auto &s : req["sourceNames"]) {
                source_names.push_back(s.get<std::string>());
            }
        }
        if (scene_name.empty()) {
            resp_error(conn, 400, "missing sceneName");
            return 400;
        }
        if (ctx->scenes->create_scene(scene_name, layout_type, source_names)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to create scene");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/scene/delete
static int handle_scene_delete(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->scenes) {
        resp_error(conn, 500, "scene_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string scene_name = req.value("sceneName", "");
        if (scene_name.empty()) {
            resp_error(conn, 400, "missing sceneName");
            return 400;
        }
        if (ctx->scenes->delete_scene(scene_name)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to delete scene");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/scene/add-source
static int handle_scene_add_source(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->scenes) {
        resp_error(conn, 500, "scene_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string scene_name = req.value("sceneName", "");
        std::string source_name = req.value("sourceName", "");
        if (scene_name.empty() || source_name.empty()) {
            resp_error(conn, 400, "missing sceneName or sourceName");
            return 400;
        }
        if (ctx->scenes->add_source(scene_name, source_name)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to add source to scene");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/scene/remove-source
static int handle_scene_remove_source(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->scenes) {
        resp_error(conn, 500, "scene_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string scene_name = req.value("sceneName", "");
        std::string source_name = req.value("sourceName", "");
        if (scene_name.empty() || source_name.empty()) {
            resp_error(conn, 400, "missing sceneName or sourceName");
            return 400;
        }
        if (ctx->scenes->remove_source(scene_name, source_name)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to remove source from scene");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// ============ Source 路由 ============

// GET /api/source/list
static int handle_source_list(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->sources) {
        resp_error(conn, 500, "source_manager not available");
        return 500;
    }

    resp_json(conn, ctx->sources->list_json());
    return 200;
}

// POST /api/source/discover
static int handle_source_discover(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->sources) {
        resp_error(conn, 500, "source_manager not available");
        return 500;
    }

    if (ctx->sources->discover()) {
        resp_ok(conn);
    } else {
        resp_error(conn, 500, "failed to discover sources");
    }
    return 200;
}

// POST /api/source/rename
static int handle_source_rename(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->sources) {
        resp_error(conn, 500, "source_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string obs_name = req.value("sourceName", "");
        std::string alias = req.value("alias", "");
        if (obs_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->sources->rename(obs_name, alias)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to rename source");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/source/configure
static int handle_source_configure(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->sources) {
        resp_error(conn, 500, "source_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string obs_name = req.value("sourceName", "");
        std::string alias = req.value("alias", "");
        std::string color_tag = req.value("colorTag", "#ffffff");
        int sort_order = req.value("sortOrder", 0);
        if (obs_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->sources->configure(obs_name, alias, color_tag, sort_order)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to configure source");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/source/show
static int handle_source_show(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->sources) {
        resp_error(conn, 500, "source_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string obs_name = req.value("sourceName", "");
        if (obs_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->sources->show(obs_name)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to show source");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/source/hide
static int handle_source_hide(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->sources) {
        resp_error(conn, 500, "source_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string obs_name = req.value("sourceName", "");
        if (obs_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->sources->hide(obs_name)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to hide source");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// ============ Marker 路由 ============

// POST /api/marker/add
static int handle_marker_add(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    std::string req_body = read_request_body(conn);
    std::string name = "未命名标记";
    std::string type = "manual";

    if (!req_body.empty()) {
        try {
            json req = json::parse(req_body);
            if (req.contains("name")) name = req["name"];
            if (req.contains("type")) type = req["type"];
        } catch (...) {
            // 忽略错误
        }
    }

    json body;
    body["id"] = "mrk_001";
    body["name"] = name;
    body["type"] = type;
    body["timecode"] = "00:00:00:00";
    body["timestamp"] = get_iso8601_timestamp();

    send_json_response(conn, 200, body);
    return 200;
}

// GET /api/marker/list
static int handle_marker_list(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    json body;
    body["markers"] = json::array();

    send_json_response(conn, 200, body);
    return 200;
}

// ============ Preset 路由 ============

// GET /api/preset/list
static int handle_preset_list(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    json body;
    body["presets"] = json::array();

    send_json_response(conn, 200, body);
    return 200;
}

// POST /api/preset/save
static int handle_preset_save(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    std::string req_body = read_request_body(conn);
    json resp;
    resp["success"] = true;
    resp["presetId"] = "pre_001";

    if (!req_body.empty()) {
        try {
            json req = json::parse(req_body);
            if (req.contains("presetName")) {
                resp["presetName"] = req["presetName"];
            }
        } catch (...) {
            // 忽略错误
        }
    }

    send_json_response(conn, 200, resp);
    return 200;
}

// ============ 音频路由 ============

// GET /api/audio/channels
static int handle_audio_channels(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->audio) {
        resp_error(conn, 500, "audio_manager not available");
        return 500;
    }

    resp_json(conn, ctx->audio->channels_json());
    return 200;
}

// POST /api/audio/volume
static int handle_audio_volume(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->audio) {
        resp_error(conn, 500, "audio_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string source_name = req.value("sourceName", "");
        double volume = req.value("volume", 1.0);
        if (source_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->audio->set_volume(source_name, volume)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to set volume");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/audio/mute
static int handle_audio_mute(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->audio) {
        resp_error(conn, 500, "audio_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string source_name = req.value("sourceName", "");
        bool muted = req.value("muted", false);
        if (source_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->audio->set_mute(source_name, muted)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to set mute");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/audio/solo
static int handle_audio_solo(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->audio) {
        resp_error(conn, 500, "audio_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string source_name = req.value("sourceName", "");
        bool solo = req.value("solo", false);
        if (source_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->audio->set_solo(source_name, solo)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to set solo");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// POST /api/audio/pan
static int handle_audio_pan(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->audio) {
        resp_error(conn, 500, "audio_manager not available");
        return 500;
    }

    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        std::string source_name = req.value("sourceName", "");
        double pan = req.value("pan", 0.0);
        if (source_name.empty()) {
            resp_error(conn, 400, "missing sourceName");
            return 400;
        }
        if (ctx->audio->set_pan(source_name, pan)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to set pan");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// GET/POST /api/audio/master-volume
static int handle_audio_master_volume(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->audio) {
        resp_error(conn, 500, "audio_manager not available");
        return 500;
    }

    if (strcmp(ri->request_method, "GET") == 0) {
        json body;
        body["volume"] = ctx->audio->master_volume();
        resp_json(conn, body);
        return 200;
    }

    // POST: set master volume
    std::string req_body = read_request_body(conn);
    if (req_body.empty()) {
        resp_error(conn, 400, "missing request body");
        return 400;
    }

    try {
        json req = json::parse(req_body);
        double volume = req.value("volume", 1.0);
        if (ctx->audio->set_master_volume(volume)) {
            resp_ok(conn);
        } else {
            resp_error(conn, 500, "failed to set master volume");
        }
        return 200;
    } catch (const json::parse_error &e) {
        resp_error(conn, 400, std::string("invalid json: ") + e.what());
        return 400;
    }
}

// ============ 录制路由（补充） ============

// POST /api/rec/pause
static int handle_rec_pause(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->recorder) {
        resp_error(conn, 500, "recorder not available");
        return 500;
    }

    if (ctx->recorder->pause()) {
        resp_ok(conn);
    } else {
        resp_error(conn, 500, "failed to pause recording");
    }
    return 200;
}

// POST /api/rec/resume
static int handle_rec_resume(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->recorder) {
        resp_error(conn, 500, "recorder not available");
        return 500;
    }

    if (ctx->recorder->resume()) {
        resp_ok(conn);
    } else {
        resp_error(conn, 500, "failed to resume recording");
    }
    return 200;
}

// GET /api/rec/status
static int handle_rec_status(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->recorder) {
        resp_error(conn, 500, "recorder not available");
        return 500;
    }

    resp_json(conn, ctx->recorder->status_json());
    return 200;
}

// ============ 时间码路由 ============

// GET /api/timecode
static int handle_timecode(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!ctx || !ctx->timecode) {
        resp_error(conn, 500, "timecode_gen not available");
        return 500;
    }

    resp_json(conn, ctx->timecode->to_json());
    return 200;
}

// ============ 叠加层路由 ============

// GET/POST /api/overlay/config
static int handle_overlay_config(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (strcmp(ri->request_method, "GET") == 0) {
        json body;
        body["opacity"] = 1.0;
        body["position"] = "top-right";
        body["showFrameCount"] = true;
        body["showTimecode"] = true;
        body["showRecordingStatus"] = true;
        body["fontSize"] = 24;
        body["fontColor"] = "#ffffff";
        resp_json(conn, body);
        return 200;
    }

    // POST: save overlay config (mock)
    std::string req_body = read_request_body(conn);
    if (!req_body.empty()) {
        try {
            json req = json::parse(req_body);
            blog_info("overlay config saved: %s", req.dump().c_str());
        } catch (...) {
            // 忽略
        }
    }
    resp_ok(conn);
    return 200;
}

// ============ 预设路由（补充） ============

// POST /api/preset/load
static int handle_preset_load(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    std::string req_body = read_request_body(conn);
    if (!req_body.empty()) {
        try {
            json req = json::parse(req_body);
            blog_info("preset load: %s", req.dump().c_str());
        } catch (...) {
            // 忽略
        }
    }
    resp_ok(conn);
    return 200;
}

// POST /api/preset/delete
static int handle_preset_delete(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    std::string req_body = read_request_body(conn);
    if (!req_body.empty()) {
        try {
            json req = json::parse(req_body);
            blog_info("preset delete: %s", req.dump().c_str());
        } catch (...) {
            // 忽略
        }
    }
    resp_ok(conn);
    return 200;
}

// ============ 设置路由 ============

// GET/POST /api/settings
static int handle_settings(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (strcmp(ri->request_method, "GET") == 0) {
        json body;
        body["recordPath"] = "C:\\Recordings";
        body["autoSplit"] = false;
        body["splitIntervalMin"] = 30;
        body["format"] = "mp4";
        body["quality"] = "high";
        body["webServerPort"] = 9527;
        body["language"] = "zh-CN";
        resp_json(conn, body);
        return 200;
    }

    // POST: save settings (mock)
    std::string req_body = read_request_body(conn);
    if (!req_body.empty()) {
        try {
            json req = json::parse(req_body);
            blog_info("settings saved: %s", req.dump().c_str());
        } catch (...) {
            // 忽略
        }
    }
    resp_ok(conn);
    return 200;
}

// ============ 静态文件服务 ============

namespace fs = std::filesystem;

// 根据文件扩展名获取 MIME type
static std::string get_mime_type(const std::string &ext) {
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css")  return "text/css";
    if (ext == ".js" || ext == ".mjs")   return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png")  return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".ico")  return "image/x-icon";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf")  return "font/ttf";
    if (ext == ".eot")  return "font/eot";
    if (ext == ".map")  return "application/json";
    return "application/octet-stream";
}

// 路径穿越安全检查
static bool is_safe_path(const std::string &relative_path) {
    return relative_path.find("..") == std::string::npos &&
           relative_path.find("\\") == std::string::npos;
}

// 读取文件并返回 HTTP 响应
static int serve_file(struct mg_connection *conn, const std::string &file_path,
                      const std::string &mime_type) {
    // 检查文件存在
    std::error_code ec;
    if (!fs::exists(file_path, ec) || !fs::is_regular_file(file_path, ec)) {
        mg_response_header_start(conn, 404);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "404 Not Found");
        return 404;
    }

    // 获取文件大小
    auto file_size = fs::file_size(file_path, ec);
    if (ec || file_size > 64 * 1024 * 1024) { // 64 MB 上限
        mg_response_header_start(conn, 500);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "500 File too large or unreadable");
        return 500;
    }

    // 读取文件
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        mg_response_header_start(conn, 500);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "500 Internal Server Error");
        return 500;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    // 发送响应
    mg_response_header_start(conn, 200);
    mg_response_header_add(conn, "Content-Type", mime_type.c_str(), -1);
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
    mg_response_header_add(conn, "Cache-Control", "public, max-age=3600", -1);
    mg_response_header_send(conn);
    mg_write(conn, content.c_str(), content.size());
    return 200;
}

// 处理 / 请求 -> 返回控制台 UI index.html
static int handle_static_index(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    // 只处理 GET
    if (strcmp(ri->request_method, "GET") != 0) {
        mg_response_header_start(conn, 405);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "405 Method Not Allowed");
        return 405;
    }

    char web_path[512];
    if (os_get_config_path(web_path, sizeof(web_path),
        "obs-studio/data/obs-plugins/obs-multicam-review/web/index.html") <= 0) {
        mg_response_header_start(conn, 500);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "500 Failed to resolve web path");
        return 500;
    }

    blog_debug("Serving index: %s", web_path);
    return serve_file(conn, web_path, "text/html");
}

// 处理 /assets/* 请求
static int handle_static_assets(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (strcmp(ri->request_method, "GET") != 0) {
        mg_response_header_start(conn, 405);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "405 Method Not Allowed");
        return 405;
    }

    // 提取 /assets/ 之后的路径
    std::string uri = ri->local_uri;
    std::string prefix = "/assets/";
    if (uri.compare(0, prefix.size(), prefix) != 0) {
        return 404;
    }

    std::string relative = uri.substr(prefix.size());
    if (relative.empty()) {
        return 404;
    }

    // 安全检查
    if (!is_safe_path(relative)) {
        blog_warn("Blocked path traversal attempt: %s", relative.c_str());
        mg_response_header_start(conn, 403);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "403 Forbidden");
        return 403;
    }

    // 构造完整路径
    char base_path[512];
    if (os_get_config_path(base_path, sizeof(base_path),
        "obs-studio/data/obs-plugins/obs-multicam-review/web") <= 0) {
        return 500;
    }

    fs::path full_path = fs::path(base_path) / "assets" / relative;
    std::string ext = full_path.extension().string();
    std::string mime = get_mime_type(ext);

    blog_debug("Serving asset: %s (%s)", full_path.string().c_str(), mime.c_str());
    return serve_file(conn, full_path.string(), mime);
}

// 处理 /overlay/* 请求
static int handle_static_overlay(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (strcmp(ri->request_method, "GET") != 0) {
        mg_response_header_start(conn, 405);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "405 Method Not Allowed");
        return 405;
    }

    // 提取 /overlay/ 之后的路径
    std::string uri = ri->local_uri;
    std::string prefix = "/overlay/";
    if (uri.compare(0, prefix.size(), prefix) != 0) {
        return 404;
    }

    std::string relative = uri.substr(prefix.size());

    // 如果无文件名，默认返回 index.html
    if (relative.empty()) {
        relative = "index.html";
    }

    // 安全检查
    if (!is_safe_path(relative)) {
        blog_warn("Blocked path traversal attempt: %s", relative.c_str());
        mg_response_header_start(conn, 403);
        mg_response_header_add(conn, "Content-Type", "text/plain", -1);
        mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
        mg_response_header_send(conn);
        mg_printf(conn, "403 Forbidden");
        return 403;
    }

    // 构造完整路径
    char base_path[512];
    if (os_get_config_path(base_path, sizeof(base_path),
        "obs-studio/data/obs-plugins/obs-multicam-review/overlay") <= 0) {
        return 500;
    }

    fs::path full_path = fs::path(base_path) / relative;
    std::string ext = full_path.extension().string();
    std::string mime = get_mime_type(ext);

    blog_debug("Serving overlay: %s (%s)", full_path.string().c_str(), mime.c_str());
    return serve_file(conn, full_path.string(), mime);
}

// ============ 数据库 API 通用辅助 ============

// 从 query string 中提取参数值
static std::string get_query_param(const struct mg_request_info *ri, const char *key) {
    if (!ri || !ri->query_string) return "";
    std::string qs(ri->query_string);
    std::string search = std::string(key) + "=";
    size_t pos = qs.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    size_t end = qs.find('&', pos);
    if (end == std::string::npos) return qs.substr(pos);
    return qs.substr(pos, end - pos);
}

// 发送结构化错误 JSON (task spec 风格)
static void send_json_error(struct mg_connection *conn, int status,
                            const char *error_code, const char *message) {
    mg_response_header_start(conn, status);
    mg_response_header_add(conn, "Content-Type", "application/json", -1);
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", -1);
    mg_response_header_add(conn, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS", -1);
    mg_response_header_add(conn, "Access-Control-Allow-Headers", "Content-Type", -1);
    mg_response_header_send(conn);
    json body;
    body["error"]["code"] = error_code;
    body["error"]["message"] = message;
    std::string body_str = body.dump();
    mg_printf(conn, "%s", body_str.c_str());
}

// 检查 database 可用性，不可用时发送 500
static bool check_database(struct mg_connection *conn, PluginContext *ctx) {
    if (!ctx || !ctx->database) {
        send_json_error(conn, 500, "ERR_DB_UNAVAILABLE", "Database not available");
        return false;
    }
    return true;
}

// ============ 项目管理 (Projects) ============

// GET /api/projects  → 项目列表
// POST /api/projects → 创建项目
static int handle_projects(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    if (strcmp(method, "GET") == 0) {
        // GET /api/projects → 返回项目列表
        char *json_out = nullptr;
        if (!db->project_list(&json_out)) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Database error");
            return 500;
        }
        json resp;
        resp["projects"] = json::parse(json_out);
        resp_json(conn, resp);
        free(json_out);
        return 200;
    }

    if (strcmp(method, "POST") == 0) {
        // POST /api/projects → 创建项目
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json req = json::parse(req_body);
            std::string name = req.value("name", "");
            if (name.empty()) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing required field: name");
                return 400;
            }
            req["id"] = gen_uuid();
            std::string input = req.dump();
            char *json_out = nullptr;
            if (!db->project_create(input.c_str(), &json_out)) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to create project");
                return 500;
            }
            json resp = json::parse(json_out);
            resp_json(conn, resp);
            free(json_out);
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
    return 405;
}

// PUT    /api/projects/:id → 更新项目
// DELETE /api/projects/:id → 删除项目
// 通过前缀 /api/projects/ 匹配，手动提取 ID
static int handle_projects_by_id(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    // 从 URI 提取 project ID: /api/projects/{id}
    const char *uri = ri->request_uri;
    std::string id = std::string(uri).substr(strlen("/api/projects/"));
    // 去掉可能的尾部斜杠和 query string
    size_t qpos = id.find('?');
    if (qpos != std::string::npos) id = id.substr(0, qpos);
    while (!id.empty() && id.back() == '/') id.pop_back();
    if (id.empty()) {
        send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing project ID");
        return 400;
    }

    if (strcmp(method, "PUT") == 0) {
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json req = json::parse(req_body);
            if (!db->project_update(id.c_str(), req_body.c_str())) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to update project");
                return 500;
            }
            // 返回更新后的项目
            char *json_out = nullptr;
            if (db->project_get(id.c_str(), &json_out)) {
                json resp = json::parse(json_out);
                resp_json(conn, resp);
                free(json_out);
            } else {
                resp_ok(conn);
            }
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    if (strcmp(method, "DELETE") == 0) {
        if (!db->project_delete(id.c_str())) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Failed to delete project");
            return 500;
        }
        resp_ok(conn);
        return 200;
    }

    send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
    return 405;
}

// ============ 产品管理 (Products) ============

// GET  /api/products?projectId=xxx → 产品列表
// POST /api/products              → 创建产品
static int handle_products(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    if (strcmp(method, "GET") == 0) {
        // GET /api/products?projectId=xxx
        std::string project_id = get_query_param(ri, "projectId");
        if (project_id.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing query parameter: projectId");
            return 400;
        }
        char *json_out = nullptr;
        if (!db->product_list(project_id.c_str(), &json_out)) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Database error");
            return 500;
        }
        json resp;
        resp["products"] = json::parse(json_out);
        resp_json(conn, resp);
        free(json_out);
        return 200;
    }

    if (strcmp(method, "POST") == 0) {
        // POST /api/products → 创建产品
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json req = json::parse(req_body);
            std::string project_id = req.value("projectId", "");
            if (project_id.empty()) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing required field: projectId");
                return 400;
            }
            std::string input = req.dump();
            char *json_out = nullptr;
            if (!db->product_create(project_id.c_str(), input.c_str(), &json_out)) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to create product");
                return 500;
            }
            json resp = json::parse(json_out);
            resp_json(conn, resp);
            free(json_out);
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
    return 405;
}

// PUT    /api/products/reorder → 重新排序
// PUT    /api/products/:id     → 更新产品
// DELETE /api/products/:id     → 删除产品
// GET    /api/products/:id/dimension-template → 获取产品绑定的维度模板
// PUT    /api/products/:id/dimension-template → 绑定维度模板
// 通过前缀 /api/products/ 匹配，手动解析路径
static int handle_products_by_path(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    // 解析路径: /api/products/{rest}
    const char *uri = ri->request_uri;
    std::string rest = std::string(uri).substr(strlen("/api/products/"));
    // 去掉 query string
    size_t qpos = rest.find('?');
    if (qpos != std::string::npos) rest = rest.substr(0, qpos);

    // /api/products/reorder
    if (rest == "reorder") {
        if (strcmp(method, "PUT") != 0) {
            send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
            return 405;
        }
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json req = json::parse(req_body);
            std::string project_id = req.value("projectId", "");
            if (project_id.empty()) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing required field: projectId");
                return 400;
            }
            std::string ids_json = req["ids"].dump();
            if (!db->product_reorder(project_id.c_str(), ids_json.c_str())) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to reorder products");
                return 500;
            }
            resp_ok(conn);
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    // /api/products/{id}/dimension-template
    size_t dim_pos = rest.find("/dimension-template");
    if (dim_pos != std::string::npos) {
        std::string product_id = rest.substr(0, dim_pos);
        if (product_id.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing product ID");
            return 400;
        }

        if (strcmp(method, "PUT") == 0) {
            // PUT /api/products/:id/dimension-template → 绑定维度模板
            std::string req_body = read_request_body(conn);
            if (req_body.empty()) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
                return 400;
            }
            try {
                json req = json::parse(req_body);
                std::string template_id = req.value("templateId", "");
                if (template_id.empty()) {
                    send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing required field: templateId");
                    return 400;
                }
                if (!db->binding_set(product_id.c_str(), template_id.c_str())) {
                    send_json_error(conn, 500, "ERR_INTERNAL", "Failed to bind dimension template");
                    return 500;
                }
                resp_ok(conn);
                return 200;
            } catch (const json::parse_error &e) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST",
                                (std::string("Invalid JSON: ") + e.what()).c_str());
                return 400;
            }
        }

        if (strcmp(method, "GET") == 0) {
            // GET /api/products/:id/dimension-template → 获取绑定的维度模板
            char *json_out = nullptr;
            if (!db->binding_get(product_id.c_str(), &json_out)) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to get dimension binding");
                return 500;
            }
            json resp = json::parse(json_out);
            resp_json(conn, resp);
            free(json_out);
            return 200;
        }

        send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
        return 405;
    }

    // /api/products/{id} → update / delete
    std::string product_id = rest;
    while (!product_id.empty() && product_id.back() == '/') product_id.pop_back();
    if (product_id.empty()) {
        send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing product ID");
        return 400;
    }

    if (strcmp(method, "PUT") == 0) {
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json::parse(req_body); // validate JSON
            if (!db->product_update(product_id.c_str(), req_body.c_str())) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to update product");
                return 500;
            }
            // 返回更新后的产品
            char *json_out = nullptr;
            if (db->product_get(product_id.c_str(), &json_out)) {
                json resp = json::parse(json_out);
                resp_json(conn, resp);
                free(json_out);
            } else {
                resp_ok(conn);
            }
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    if (strcmp(method, "DELETE") == 0) {
        if (!db->product_delete(product_id.c_str())) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Failed to delete product");
            return 500;
        }
        resp_ok(conn);
        return 200;
    }

    send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
    return 405;
}

// ============ 评分维度模板 (Dimension Templates) ============

// GET    /api/dimensions/templates    → 列出所有模板
// POST   /api/dimensions/templates    → 创建模板
// DELETE /api/dimensions/templates/:id → 删除模板 (通过前缀匹配)
static int handle_dim_templates(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    if (strcmp(method, "GET") == 0) {
        // GET /api/dimensions/templates → 列出模板
        char *json_out = nullptr;
        if (!db->dim_template_list(&json_out)) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Database error");
            return 500;
        }
        json resp;
        resp["templates"] = json::parse(json_out);
        resp_json(conn, resp);
        free(json_out);
        return 200;
    }

    if (strcmp(method, "POST") == 0) {
        // POST /api/dimensions/templates → 创建模板
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json req = json::parse(req_body);
            std::string name = req.value("name", "");
            if (name.empty()) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing required field: name");
                return 400;
            }
            char *json_out = nullptr;
            if (!db->dim_template_create(req_body.c_str(), &json_out)) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to create template");
                return 500;
            }
            json resp = json::parse(json_out);
            resp_json(conn, resp);
            free(json_out);
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    // DELETE /api/dimensions/templates/:id → 检查是否有路径片段
    const char *uri = ri->request_uri;
    std::string rest = std::string(uri).substr(strlen("/api/dimensions/templates/"));
    size_t qpos = rest.find('?');
    if (qpos != std::string::npos) rest = rest.substr(0, qpos);
    while (!rest.empty() && rest.back() == '/') rest.pop_back();

    if (!rest.empty() && strcmp(method, "DELETE") == 0) {
        // DELETE /api/dimensions/templates/:id
        if (!db->dim_template_delete(rest.c_str())) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Failed to delete template");
            return 500;
        }
        resp_ok(conn);
        return 200;
    }

    send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
    return 405;
}

// ============ 评分会话 (Scoring Sessions) ============

// POST /api/scoring/sessions          → 创建评分会话
// GET  /api/scoring/sessions/:id       → 获取会话详情
// POST /api/scoring/sessions/:id/complete → 完成会话
static int handle_scoring_sessions(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    const char *uri = ri->request_uri;

    // POST /api/scoring/sessions → 创建会话 (精确匹配基础路径)
    if (strcmp(uri, "/api/scoring/sessions") == 0 && strcmp(method, "POST") == 0) {
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json req = json::parse(req_body);
            std::string project_id = req.value("projectId", "");
            if (project_id.empty()) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing required field: projectId");
                return 400;
            }
            char *json_out = nullptr;
            if (!db->scoring_session_create(req_body.c_str(), &json_out)) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to create scoring session");
                return 500;
            }
            json resp = json::parse(json_out);
            resp_json(conn, resp);
            free(json_out);
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    // 带有子路径的: /api/scoring/sessions/{id}[/complete]
    std::string rest = std::string(uri).substr(strlen("/api/scoring/sessions/"));
    size_t qpos = rest.find('?');
    if (qpos != std::string::npos) rest = rest.substr(0, qpos);

    if (rest.empty()) {
        send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
        return 405;
    }

    // 检查是否以 /complete 结尾
    size_t complete_pos = rest.rfind("/complete");
    bool is_complete = (complete_pos != std::string::npos && complete_pos == rest.size() - strlen("/complete"));
    std::string session_id;
    if (is_complete) {
        session_id = rest.substr(0, complete_pos);
    } else {
        session_id = rest;
    }
    while (!session_id.empty() && session_id.back() == '/') session_id.pop_back();

    if (session_id.empty()) {
        send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing session ID");
        return 400;
    }

    if (is_complete && strcmp(method, "POST") == 0) {
        // POST /api/scoring/sessions/:id/complete
        if (!db->scoring_session_complete(session_id.c_str())) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Failed to complete scoring session");
            return 500;
        }
        resp_ok(conn);
        return 200;
    }

    if (!is_complete && strcmp(method, "GET") == 0) {
        // GET /api/scoring/sessions/:id
        char *json_out = nullptr;
        if (!db->scoring_session_get(session_id.c_str(), &json_out)) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Failed to get scoring session");
            return 500;
        }
        json resp = json::parse(json_out);
        resp_json(conn, resp);
        free(json_out);
        return 200;
    }

    send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
    return 405;
}

// ============ 评分提交 & 排行榜 ============

// POST /api/scoring/scores → 提交评分
// GET  /api/scoring/scores?sessionId=x&productId=y → 查询评分
static int handle_scoring_scores(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    if (strcmp(method, "POST") == 0) {
        // POST /api/scoring/scores → 提交评分
        std::string req_body = read_request_body(conn);
        if (req_body.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing request body");
            return 400;
        }
        try {
            json req = json::parse(req_body);
            std::string session_id = req.value("sessionId", "");
            std::string product_id = req.value("productId", "");
            std::string dim_key = req.value("dimKey", "");
            double score = req.value("score", -1.0);
            std::string note = req.value("note", "");

            if (session_id.empty() || product_id.empty() || dim_key.empty() || score < 0) {
                send_json_error(conn, 400, "ERR_BAD_REQUEST",
                                "Missing required fields: sessionId, productId, dimKey, score");
                return 400;
            }
            if (!db->score_submit(session_id.c_str(), product_id.c_str(),
                                  dim_key.c_str(), score, note.c_str())) {
                send_json_error(conn, 500, "ERR_INTERNAL", "Failed to submit score");
                return 500;
            }
            resp_ok(conn);
            return 200;
        } catch (const json::parse_error &e) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            (std::string("Invalid JSON: ") + e.what()).c_str());
            return 400;
        }
    }

    if (strcmp(method, "GET") == 0) {
        // GET /api/scoring/scores?sessionId=x&productId=y
        std::string session_id = get_query_param(ri, "sessionId");
        std::string product_id = get_query_param(ri, "productId");
        if (session_id.empty() || product_id.empty()) {
            send_json_error(conn, 400, "ERR_BAD_REQUEST",
                            "Missing query parameters: sessionId and productId");
            return 400;
        }
        char *json_out = nullptr;
        if (!db->score_get_by_product(session_id.c_str(), product_id.c_str(), &json_out)) {
            send_json_error(conn, 500, "ERR_INTERNAL", "Failed to get scores");
            return 500;
        }
        json resp;
        resp["scores"] = json::parse(json_out);
        resp_json(conn, resp);
        free(json_out);
        return 200;
    }

    send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
    return 405;
}

// GET /api/scoring/leaderboard?sessionId=x → 排行榜
static int handle_leaderboard(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *method = ri->request_method;

    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(conn);
        return 200;
    }

    if (!check_database(conn, ctx)) return 500;
    auto *db = ctx->database;

    if (strcmp(method, "GET") != 0) {
        send_json_error(conn, 405, "ERR_METHOD", "Method not allowed");
        return 405;
    }

    std::string session_id = get_query_param(ri, "sessionId");
    if (session_id.empty()) {
        send_json_error(conn, 400, "ERR_BAD_REQUEST", "Missing query parameter: sessionId");
        return 400;
    }

    char *json_out = nullptr;
    if (!db->score_get_leaderboard(session_id.c_str(), &json_out)) {
        send_json_error(conn, 500, "ERR_INTERNAL", "Failed to get leaderboard");
        return 500;
    }
    json resp;
    resp["leaderboard"] = json::parse(json_out);
    resp_json(conn, resp);
    free(json_out);
    return 200;
}

// ============ WebSocket 处理器 ============

// 前向声明（定义在 ws_close_handler 之后）
static void ws_send(struct mg_connection *conn, const std::string &action,
                    const json &payload, const std::string &msg_id = "");
static void ws_broadcast_event(const std::string &action, const json &payload);

static int ws_connect_handler(const struct mg_connection *conn, void *cbdata) {
    (void)conn;
    (void)cbdata;
    blog_info("WebSocket client connecting");
    return 0; // 0 = accept
}

static void ws_ready_handler(struct mg_connection *conn, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);

    // 创建会话
    {
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        WsSession session;
        session.conn = const_cast<struct mg_connection *>(conn);
        session.plugin_ctx = ctx;
        session.last_pong = time(nullptr);
        session.active = true;
        g_ws_sessions[session.conn] = session;
    }

    blog_info("WebSocket client connected");

    // 发送 system.status 事件
    const char *rec_state = "idle";
    if (ctx && ctx->recording_active) {
        rec_state = "recording";
    }

    json status_payload;
    status_payload["pluginVersion"] = PLUGIN_VERSION;
    status_payload["obsVersion"] = "30.2.0";
    status_payload["recordingState"] = rec_state;
    status_payload["currentScene"] = "main_full";
    status_payload["timecode"] = "00:00:00:00";
    status_payload["fps"] = 60;
    status_payload["diskFreeBytes"] = 262144000000LL;
    status_payload["cpuUsage"] = 12.5;
    status_payload["memoryUsageMB"] = 256;

    json env = build_envelope("event", "system.status", status_payload);
    std::string msg = env.dump();
    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.size());
}

static int ws_data_handler(struct mg_connection *conn, int flags,
                           char *data, size_t data_len, void *cbdata) {
    PluginContext *ctx = static_cast<PluginContext *>(cbdata);

    // 检查是否为关闭帧
    if ((flags & 0x0F) == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE) {
        return 1; // 关闭连接
    }

    // 解析 JSON 消息
    std::string msg_str(data, data_len);
    json msg;
    try {
        msg = json::parse(msg_str);
    } catch (const json::parse_error &e) {
        blog_warn("Failed to parse WS message: %s", e.what());
        return 1;
    }

    std::string action = msg.value("action", "");
    std::string msg_id = msg.value("id", "");
    std::string msg_type = msg.value("type", "request");

    blog_debug("WS received: type=%s action=%s", msg_type.c_str(), action.c_str());

    // system.pong → 心跳回复
    if (action == "system.pong") {
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        auto it = g_ws_sessions.find(conn);
        if (it != g_ws_sessions.end()) {
            it->second.last_pong = time(nullptr);
        }
        return 1; // keep alive
    }

    // system.heartbeat → 忽略 (服务器主动发送)
    if (action == "system.heartbeat") {
        return 1;
    }

    // ============ 指令处理 ============
    blog_info("[ws] command: %s", action.c_str());

    // rec.start → 启动录制
    if (action == "rec.start") {
        if (!ctx || !ctx->recorder) {
            ws_send(conn, "rec.start", {{"error", "recorder not available"}}, msg_id);
            return 1;
        }
        std::string output_dir = msg.value("payload", json::object()).value("outputDir",
            std::string(getenv("USERPROFILE") ? getenv("USERPROFILE") : ".") + "\\Recordings");
        bool ok = ctx->recorder->start(output_dir, "", true);
        if (ok) {
            auto st = ctx->recorder->status_json();
            ws_send(conn, "rec.start", st, msg_id);
            ws_broadcast_event("recording.started", st);
        } else {
            ws_send(conn, "rec.start", {{"error", "Failed to start recording"}}, msg_id);
        }
        return 1;
    }

    // rec.stop
    if (action == "rec.stop") {
        if (!ctx || !ctx->recorder) {
            ws_send(conn, "rec.stop", {{"error", "recorder not available"}}, msg_id);
            return 1;
        }
        bool ok = ctx->recorder->stop();
        auto st = ctx->recorder->status_json();
        if (ok) {
            ws_send(conn, "rec.stop", st, msg_id);
            ws_broadcast_event("recording.stopped", st);
        } else {
            ws_send(conn, "rec.stop", {{"error", "Not recording"}}, msg_id);
        }
        return 1;
    }

    // rec.pause
    if (action == "rec.pause") {
        if (!ctx || !ctx->recorder) {
            ws_send(conn, "rec.pause", {{"error", "recorder not available"}}, msg_id);
            return 1;
        }
        ctx->recorder->pause();
        json payload = {{"recordingId", ctx->recorder->status().recording_id}};
        ws_send(conn, "rec.pause", payload, msg_id);
        ws_broadcast_event("recording.paused", payload);
        return 1;
    }

    // rec.resume
    if (action == "rec.resume") {
        if (!ctx || !ctx->recorder) {
            ws_send(conn, "rec.resume", {{"error", "recorder not available"}}, msg_id);
            return 1;
        }
        ctx->recorder->resume();
        json payload = {{"recordingId", ctx->recorder->status().recording_id}};
        ws_send(conn, "rec.resume", payload, msg_id);
        ws_broadcast_event("recording.resumed", payload);
        return 1;
    }

    // scene.switch
    if (action == "scene.switch") {
        std::string scene_name = msg["payload"].value("sceneName", "");
        if (!ctx || !ctx->scenes || scene_name.empty()) {
            ws_send(conn, "scene.switch",
                {{"error", scene_name.empty() ? "missing sceneName" : "scene_manager not available"}}, msg_id);
            return 1;
        }
        bool ok = ctx->scenes->switch_to(scene_name);
        json payload = {{"sceneName", scene_name}, {"success", ok}};
        ws_send(conn, "scene.switch", payload, msg_id);
        if (ok) ws_broadcast_event("scene.changed", payload);
        return 1;
    }

    // marker.add
    if (action == "marker.add") {
        std::string name = msg["payload"].value("name", "Marker");
        json payload;
        payload["id"] = gen_uuid();
        payload["name"] = name;
        payload["timecode"] = ctx && ctx->timecode ? ctx->timecode->smpte() : "00:00:00:00";
        payload["timestamp"] = get_iso8601_timestamp();
        ws_send(conn, "marker.add", payload, msg_id);
        ws_broadcast_event("marker.added", payload);
        return 1;
    }

    // preset.load
    if (action == "preset.load") {
        std::string preset_id = msg["payload"].value("presetId", "");
        if (!ctx || !ctx->presets || preset_id.empty()) {
            ws_send(conn, "preset.load",
                {{"error", preset_id.empty() ? "missing presetId" : "preset_manager not available"}}, msg_id);
            return 1;
        }
        bool ok = ctx->presets->load(preset_id);
        json payload;
        payload["loaded"] = ok;
        payload["presetId"] = preset_id;
        ws_send(conn, "preset.load", payload, msg_id);
        return 1;
    }

    // source.show
    if (action == "source.show") {
        std::string src_name = msg["payload"].value("sourceName", "");
        if (!ctx || !ctx->sources || src_name.empty()) {
            ws_send(conn, "source.show",
                {{"error", src_name.empty() ? "missing sourceName" : "source_manager not available"}}, msg_id);
            return 1;
        }
        ctx->sources->show(src_name);
        ws_send(conn, "source.show", {{"sourceName", src_name}, {"ok", true}}, msg_id);
        return 1;
    }

    // source.hide
    if (action == "source.hide") {
        std::string src_name = msg["payload"].value("sourceName", "");
        if (!ctx || !ctx->sources || src_name.empty()) {
            ws_send(conn, "source.hide",
                {{"error", src_name.empty() ? "missing sourceName" : "source_manager not available"}}, msg_id);
            return 1;
        }
        ctx->sources->hide(src_name);
        ws_send(conn, "source.hide", {{"sourceName", src_name}, {"ok", true}}, msg_id);
        return 1;
    }

    // 未识别的指令
    blog_warn("[ws] unknown command: %s", action.c_str());
    ws_send(conn, action, {{"error", "unknown command: " + action}}, msg_id);

    return 1; // keep alive
}

static void ws_close_handler(const struct mg_connection *conn, void *cbdata) {
    (void)cbdata;

    {
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        g_ws_sessions.erase(const_cast<struct mg_connection *>(conn));
    }

    blog_info("WebSocket client disconnected");
}

// ============ 广播函数 ============

// 向所有活跃 WebSocket 客户端广播事件
static void ws_broadcast_event(const std::string &action, const json &payload) {
    json env = build_envelope("event", action, payload);
    std::string msg = env.dump();

    std::lock_guard<std::mutex> lock(g_ws_mutex);
    for (auto &pair : g_ws_sessions) {
        if (pair.second.active) {
            mg_websocket_write(pair.second.conn,
                MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.size());
        }
    }
}

// 向单个客户端发送消息
static void ws_send(struct mg_connection *conn, const std::string &action,
                    const json &payload, const std::string &msg_id = "") {
    std::string type = msg_id.empty() ? "event" : "response";
    json env = build_envelope(type, action, payload, msg_id);
    std::string msg = env.dump();
    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.size());
}

// ============ 心跳线程 ============

static void heartbeat_thread_func() {
    blog_info("Tick thread started");

    auto last_heartbeat = std::chrono::steady_clock::now();

    while (g_heartbeat_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto now = std::chrono::steady_clock::now();

        // 心跳每 15 秒
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count() >= 15) {
            last_heartbeat = now;

            std::lock_guard<std::mutex> lock(g_ws_mutex);
            time_t t_now = time(nullptr);
            for (auto it = g_ws_sessions.begin(); it != g_ws_sessions.end(); ) {
                auto &session = it->second;
                time_t elapsed = t_now - session.last_pong;
                if (elapsed > kWebSocketTimeoutSec) {
                    blog_warn("WS timeout, closing");
                    session.active = false;
                    mg_websocket_write(session.conn, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, nullptr, 0);
                    it = g_ws_sessions.erase(it);
                } else {
                    json hb = build_envelope("event", "system.heartbeat", json::object());
                    std::string hb_str = hb.dump();
                    mg_websocket_write(session.conn, MG_WEBSOCKET_OPCODE_TEXT, hb_str.c_str(), hb_str.size());
                    ++it;
                }
            }
        }

        // 时间码广播每 100ms (录制中才发)
        if (g_ctx && g_ctx->recording_active && g_ctx->timecode) {
            json tc;
            tc["timecode"] = g_ctx->timecode->smpte();
            tc["frameIndex"] = g_ctx->timecode->frame_number();
            ws_broadcast_event("timecode.tick", tc);
        }
    }

    blog_info("Tick thread stopped");
}

// ============ 公开接口 ============

bool ws_start(PluginContext *ctx) {
    if (!ctx) {
        blog_error("ws_start: null context");
        return false;
    }

    if (ctx->web_server && ctx->web_server->running) {
        blog_warn("ws_start: server already running");
        return true;
    }

    blog_info("Starting web server on port %d...", WEB_SERVER_PORT);

    // 分配 WebServer 状态
    if (!ctx->web_server) {
        ctx->web_server = new WebServer();
    }

    // 配置选项
    const char *options[] = {
        "listening_ports", "9527",
        "num_threads", "4",
        "enable_keep_alive", "yes",
        "enable_directory_listing", "no",
        nullptr
    };

    // 启动 civetweb
    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    ctx->web_server->ctx = mg_start(&callbacks, ctx, options);
    if (!ctx->web_server->ctx) {
        blog_error("Failed to start civetweb server");
        return false;
    }

    // 注册 REST 路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/system/health",
                           handle_health, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/system/status",
                           handle_system_status, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/rec/start",
                           handle_rec_start, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/rec/stop",
                           handle_rec_stop, ctx);

    // Scene 路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/scene/list",
                           handle_scene_list, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/scene/switch",
                           handle_scene_switch, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/scene/create",
                           handle_scene_create, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/scene/delete",
                           handle_scene_delete, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/scene/add-source",
                           handle_scene_add_source, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/scene/remove-source",
                           handle_scene_remove_source, ctx);

    // Source 路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/source/list",
                           handle_source_list, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/source/discover",
                           handle_source_discover, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/source/rename",
                           handle_source_rename, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/source/configure",
                           handle_source_configure, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/source/show",
                           handle_source_show, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/source/hide",
                           handle_source_hide, ctx);

    // Marker 路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/marker/add",
                           handle_marker_add, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/marker/list",
                           handle_marker_list, ctx);

    // Preset 路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/preset/list",
                           handle_preset_list, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/preset/save",
                           handle_preset_save, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/preset/load",
                           handle_preset_load, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/preset/delete",
                           handle_preset_delete, ctx);

    // 音频路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/audio/channels",
                           handle_audio_channels, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/audio/volume",
                           handle_audio_volume, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/audio/mute",
                           handle_audio_mute, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/audio/solo",
                           handle_audio_solo, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/audio/pan",
                           handle_audio_pan, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/audio/master-volume",
                           handle_audio_master_volume, ctx);

    // 录制路由（补充）
    mg_set_request_handler(ctx->web_server->ctx, "/api/rec/pause",
                           handle_rec_pause, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/rec/resume",
                           handle_rec_resume, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/rec/status",
                           handle_rec_status, ctx);

    // 时间码路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/timecode",
                           handle_timecode, ctx);

    // 叠加层路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/overlay/config",
                           handle_overlay_config, ctx);

    // 设置路由
    mg_set_request_handler(ctx->web_server->ctx, "/api/settings",
                           handle_settings, ctx);

    // ============ 数据库 API 路由 ============

    // 项目管理
    mg_set_request_handler(ctx->web_server->ctx, "/api/projects",
                           handle_projects, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/projects/",
                           handle_projects_by_id, ctx);

    // 产品管理
    mg_set_request_handler(ctx->web_server->ctx, "/api/products",
                           handle_products, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/products/",
                           handle_products_by_path, ctx);

    // 评分维度模板
    mg_set_request_handler(ctx->web_server->ctx, "/api/dimensions/templates",
                           handle_dim_templates, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/dimensions/templates/",
                           handle_dim_templates, ctx);

    // 评分会话
    mg_set_request_handler(ctx->web_server->ctx, "/api/scoring/sessions",
                           handle_scoring_sessions, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/api/scoring/sessions/",
                           handle_scoring_sessions, ctx);

    // 评分提交
    mg_set_request_handler(ctx->web_server->ctx, "/api/scoring/scores",
                           handle_scoring_scores, ctx);

    // 排行榜
    mg_set_request_handler(ctx->web_server->ctx, "/api/scoring/leaderboard",
                           handle_leaderboard, ctx);

    // 静态文件服务 - 控制台 UI
    mg_set_request_handler(ctx->web_server->ctx, "/",
                           handle_static_index, ctx);
    mg_set_request_handler(ctx->web_server->ctx, "/assets/",
                           handle_static_assets, ctx);

    // 静态文件服务 - 叠加层
    mg_set_request_handler(ctx->web_server->ctx, "/overlay/",
                           handle_static_overlay, ctx);

    blog_info("Registered %d REST routes + 3 static file routes", 47);

    // 注册 WebSocket
    mg_set_websocket_handler(ctx->web_server->ctx, "/ws",
                             ws_connect_handler,
                             ws_ready_handler,
                             ws_data_handler,
                             ws_close_handler,
                             ctx);
    blog_info("WebSocket handler registered at /ws");

    // 启动心跳线程
    g_heartbeat_running.store(true);
    g_heartbeat_thread = std::thread(heartbeat_thread_func);

    ctx->web_server->running = true;
    blog_info("Web server started successfully on port %d", WEB_SERVER_PORT);
    return true;
}

void ws_stop(WebServer *ws) {
    if (!ws || !ws->running) {
        return;
    }

    blog_info("Stopping web server...");

    // 停止心跳线程
    g_heartbeat_running.store(false);
    if (g_heartbeat_thread.joinable()) {
        g_heartbeat_thread.join();
    }

    // 关闭所有 WebSocket 连接
    {
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        g_ws_sessions.clear();
    }

    // 停止 civetweb
    if (ws->ctx) {
        mg_stop(ws->ctx);
        ws->ctx = nullptr;
    }

    ws->running = false;
    blog_info("Web server stopped");
}
