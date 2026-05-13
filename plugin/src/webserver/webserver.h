#pragma once

#include <string>
#include <functional>

struct mg_context;
struct PluginContext;

// Web 服务器模块状态
struct WebServer {
    mg_context *ctx = nullptr;
    bool running = false;
};

// 生命周期
bool ws_start(PluginContext *ctx);
void ws_stop(WebServer *ws);
