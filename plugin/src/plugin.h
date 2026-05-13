#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>

#define PLUGIN_NAME    "obs-multicam-review"
#define PLUGIN_VERSION "1.0.0"
#define PLUGIN_AUTHOR  "OpenClaw AI"
#define PLUGIN_WEBSITE "https://github.com/openclaw/obs-multicam-review"

// Web 服务器配置
#define WEB_SERVER_PORT   9527
#define WEB_SERVER_THREADS 4

// 前向声明
struct PluginContext;
struct WebServer;
struct Recorder;
struct TimecodeGen;
struct SceneManager;
namespace multicam { class SourceManager; class Database; class AudioManager; }
struct AudioConsole;
struct PresetManager;

// 插件全局上下文
struct PluginContext {
    // 模块
    WebServer     *web_server;
    Recorder      *recorder;
    TimecodeGen   *timecode;
    SceneManager  *scenes;
    multicam::SourceManager *sources;
    multicam::Database *database;
    multicam::AudioManager *audio;
    AudioConsole  *audio_console;
    PresetManager *presets;

    // 状态
    bool recording_active;
    uint32_t frame_count;
    obs_output_t *pgm_output;

    // 回调
    obs_source_t *main_source;
};

// 全局单例
extern PluginContext *g_ctx;
