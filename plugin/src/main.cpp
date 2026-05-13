#include "plugin.h"
#include "database/database.h"
#include "webserver/webserver.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/dstr.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-multicam-review", "en-US")

PluginContext *g_ctx = nullptr;

// ============ 日志辅助 ============
#define blog_info(fmt, ...)  blog(LOG_INFO,    "[multicam] " fmt, ##__VA_ARGS__)
#define blog_warn(fmt, ...)  blog(LOG_WARNING, "[multicam] " fmt, ##__VA_ARGS__)
#define blog_error(fmt, ...) blog(LOG_ERROR,   "[multicam] " fmt, ##__VA_ARGS__)

// ============ 前端事件回调 ============
static void on_event(enum obs_frontend_event event, void *data) {
    (void)data;

    switch (event) {
    case OBS_FRONTEND_EVENT_RECORDING_STARTED:
        blog_info("OBS recording started");
        if (g_ctx) g_ctx->recording_active = true;
        break;

    case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
        blog_info("OBS recording stopped");
        if (g_ctx) g_ctx->recording_active = false;
        break;

    case OBS_FRONTEND_EVENT_EXIT:
        blog_info("OBS exiting, cleaning up");
        break;

    default:
        break;
    }
}

// ============ OBS 插件入口 ============
bool obs_module_load(void) {
    blog_info("Loading obs-multicam-review v" PLUGIN_VERSION);

    // 分配全局上下文
    g_ctx = (PluginContext *)bzalloc(sizeof(PluginContext));
    if (!g_ctx) {
        blog_error("Failed to allocate plugin context");
        return false;
    }

    // 注册前端事件
    obs_frontend_add_event_callback(on_event, nullptr);

    // 初始化 Web 服务器 (civetweb)
    if (!ws_start(g_ctx)) {
        blog_error("Failed to start web server");
        obs_frontend_remove_event_callback(on_event, nullptr);
        bfree(g_ctx);
        g_ctx = nullptr;
        return false;
    }

    // ---- 初始化数据库 ----
    g_ctx->database = new multicam::Database();
    char db_path[512];
    if (os_get_config_path(db_path, sizeof(db_path),
        "obs-studio/plugin_data/obs-multicam-review/multicam.db") > 0) {
        if (g_ctx->database->open(db_path) && g_ctx->database->init_schema()) {
            blog_info("Database initialized: %s", db_path);
        } else {
            blog_error("Failed to initialize database");
            delete g_ctx->database;
            g_ctx->database = nullptr;
        }
    } else {
        blog_error("Failed to get database path");
    }

    // TODO: 初始化其他模块
    // - 时间码生成器
    // - 录制引擎
    // - 场景管理器
    // - 音频控制台
    // - 预设管理器

    blog_info("obs-multicam-review loaded successfully");
    return true;
}

void obs_module_unload(void) {
    blog_info("Unloading obs-multicam-review");

    if (g_ctx) {
        // 销毁数据库
        if (g_ctx->database) {
            g_ctx->database->close();
            delete g_ctx->database;
            g_ctx->database = nullptr;
        }

        // 停止 Web 服务器
        ws_stop(g_ctx->web_server);
        if (g_ctx->web_server) {
            delete g_ctx->web_server;
            g_ctx->web_server = nullptr;
        }

        obs_frontend_remove_event_callback(on_event, nullptr);
        bfree(g_ctx);
        g_ctx = nullptr;
    }

    blog_info("obs-multicam-review unloaded");
}

const char *obs_module_name(void) {
    return "多机位评测录像系统";
}

const char *obs_module_description(void) {
    return "一站式多机位评测录像解决方案 — 支持多源采集、时间码同步、音频控制台、评分系统、预设管理";
}

const char *obs_module_author(void) {
    return PLUGIN_AUTHOR;
}
