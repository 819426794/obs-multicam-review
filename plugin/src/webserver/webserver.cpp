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

// ============ WebSocket 处理器 ============

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

    // 处理 §4.4 指令（当前阶段打印日志 + 返回 system.status）
    blog_info("WS command: %s (Phase 1: mock response)", action.c_str());

    // 构建 system.status 响应
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

    // 如果是 request，返回 response；否则返回 event
    std::string resp_type = (msg_type == "request") ? "response" : "event";
    json env = build_envelope(resp_type, "system.status", status_payload, msg_id);
    std::string resp_str = env.dump();
    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, resp_str.c_str(), resp_str.size());

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

// ============ 心跳线程 ============

static void heartbeat_thread_func() {
    blog_info("Heartbeat thread started");

    while (g_heartbeat_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(15));

        std::lock_guard<std::mutex> lock(g_ws_mutex);
        time_t now = time(nullptr);

        auto it = g_ws_sessions.begin();
        while (it != g_ws_sessions.end()) {
            auto &session = it->second;
            time_t elapsed = now - session.last_pong;

            if (elapsed > kWebSocketTimeoutSec) {
                // 超时，关闭连接
                blog_warn("WS client timeout (%llds since last pong), closing",
                          static_cast<long long>(elapsed));
                session.active = false;
                mg_websocket_write(session.conn,
                                   MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE,
                                   nullptr, 0);
                it = g_ws_sessions.erase(it);
            } else {
                // 发送心跳
                json heartbeat;
                heartbeat["type"] = "event";
                heartbeat["id"] = gen_uuid();
                heartbeat["action"] = "system.heartbeat";
                heartbeat["payload"] = json::object();
                heartbeat["timestamp"] = get_iso8601_timestamp();

                std::string hb_str = heartbeat.dump();
                mg_websocket_write(session.conn,
                                   MG_WEBSOCKET_OPCODE_TEXT,
                                   hb_str.c_str(), hb_str.size());
                ++it;
            }
        }
    }

    blog_info("Heartbeat thread stopped");
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

    blog_info("Registered %d REST routes", 37);

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
