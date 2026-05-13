#include "source/source_manager.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

namespace multicam {

// ──────────────────────────────────────────────
//  内部工具函数
// ──────────────────────────────────────────────

static constexpr double kSilenceDB = -120.0;

static bool source_enum_callback(void *param, obs_source_t *source)
{
    auto *sources = static_cast<std::vector<SourceInfo> *>(param);
    if (!source)
        return true;

    // 跳过场景（场景是容器，不是输入源）
    const char *obs_id = obs_source_get_id(source);
    if (std::strcmp(obs_id, "scene") == 0)
        return true;

    obs_source_t *ref = obs_source_get_ref(source);
    if (!ref)
        return true;

    const char *name = obs_source_get_name(source);
    uint32_t w = obs_source_get_width(source);
    uint32_t h = obs_source_get_height(source);
    bool active = obs_source_active(source);

    // fps 从设置中读取，OBS C 没有直接 API，
    // 此处返回 0 表示未知，caller 可二次填充
    double fps = 0.0;
    obs_data_t *settings = obs_source_get_settings(source);
    if (settings) {
        fps = obs_data_get_double(settings, "fps");
        obs_data_release(settings);
    }

    // 音频信息
    double audio_level = kSilenceDB;
    bool muted = false;
    uint32_t audio_flags = obs_source_get_output_flags(source);
    bool has_audio = (audio_flags & OBS_SOURCE_AUDIO) != 0;
    if (has_audio) {
        muted = obs_source_muted(source);
        // 尝试获取音量 dB（obs_source_get_volume 返回 0.0~1.0 倍率）
        float vol = obs_source_get_volume(source);
        if (vol > 0.0f)
            audio_level = 20.0 * std::log10(vol);
    }

    SourceInfo info;
    info.id = obs_source_get_uuid(source);
    info.obs_name = name ? name : "";
    info.alias = "";
    info.type = obs_id ? obs_id : "";
    info.category = "";  // 由 SourceManager::categorize_source 填充
    info.active = active;
    info.showing = obs_source_enabled(source);
    info.width = static_cast<int>(w);
    info.height = static_cast<int>(h);
    info.fps = fps;
    info.audio_level = audio_level;
    info.muted = muted;
    info.color_tag = "#ffffff";
    info.sort_order = 0;

    sources->push_back(std::move(info));
    obs_source_release(ref);
    return true;
}

// ──────────────────────────────────────────────
//  SourceManager 实现
// ──────────────────────────────────────────────

std::string SourceManager::categorize_source(const std::string &obs_id)
{
    // camera
    if (obs_id == "dshow_input"
        || obs_id == "v4l2_input"
        || obs_id == "decklink-input"
        || obs_id == "ndi_source")
    {
        return "camera";
    }

    // desktop
    if (obs_id == "monitor_capture"
        || obs_id == "screen_capture_xcomposite")
    {
        return "desktop";
    }

    // window
    if (obs_id == "window_capture"
        || obs_id == "game_capture"
        || obs_id == "pipewire-window-capture-source")
    {
        return "window";
    }

    // browser
    if (obs_id == "browser_source")
    {
        return "browser";
    }

    // media
    if (obs_id == "ffmpeg_source"
        || obs_id == "image_source"
        || obs_id == "slideshow"
        || obs_id == "vlc_source"
        || obs_id == "text_ft2_source"
        || obs_id == "text_gdiplus")
    {
        return "media";
    }

    // audio
    if (obs_id == "wasapi_input_capture"
        || obs_id == "wasapi_output_capture"
        || obs_id == "pulse_input_capture"
        || obs_id == "pulse_output_capture"
        || obs_id == "coreaudio_input_capture"
        || obs_id == "coreaudio_output_capture"
        || obs_id == "jack_input_capture")
    {
        return "audio";
    }

    // ndi — dedicated ndi plugins
    if (obs_id.rfind("ndi", 0) == 0)
    {
        return "ndi";
    }

    return "unknown";
}

bool SourceManager::discover()
{
    std::lock_guard<std::mutex> lock(mutex_);

    sources_.clear();
    blog_info("[source] discovering all OBS sources...");

    obs_enum_sources(source_enum_callback, &sources_);

    // 分类并填充 category
    for (auto &src : sources_) {
        src.category = categorize_source(src.type);
        blog_info("[source] found '%s' (type=%s, cat=%s)",
                  src.obs_name.c_str(), src.type.c_str(), src.category.c_str());
    }

    blog_info("[source] discover complete: %zu source(s)", sources_.size());
    return true;
}

std::vector<SourceInfo> SourceManager::list()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_;
}

bool SourceManager::rename(const std::string &obs_name, const std::string &alias)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto &src : sources_) {
        if (src.obs_name == obs_name) {
            src.alias = alias;
            blog_info("[source] renamed '%s' -> alias '%s'",
                      obs_name.c_str(), alias.c_str());
            return true;
        }
    }

    blog_warn("[source] rename failed: source '%s' not found", obs_name.c_str());
    return false;
}

bool SourceManager::configure(const std::string &obs_name, const std::string &alias,
                               const std::string &color_tag, int sort_order)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto &src : sources_) {
        if (src.obs_name == obs_name) {
            src.alias = alias;
            src.color_tag = color_tag;
            src.sort_order = sort_order;
            blog_info("[source] configured '%s': alias='%s' color=%s order=%d",
                      obs_name.c_str(), alias.c_str(),
                      color_tag.c_str(), sort_order);
            return true;
        }
    }

    blog_warn("[source] configure failed: source '%s' not found", obs_name.c_str());
    return false;
}

bool SourceManager::show(const std::string &obs_name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 查找并设置 OBS 源可见
    auto toggle_cb = [](void *param, obs_source_t *source) -> bool {
        const char *target = static_cast<const char *>(param);
        const char *name = obs_source_get_name(source);
        if (name && std::strcmp(name, target) == 0) {
            obs_source_set_enabled(source, true);
            return false;  // 找到后停止枚举
        }
        return true;
    };

    obs_enum_sources(toggle_cb, const_cast<char *>(obs_name.c_str()));

    // 更新内部记录
    for (auto &src : sources_) {
        if (src.obs_name == obs_name) {
            src.showing = true;
            blog_info("[source] show '%s'", obs_name.c_str());
            return true;
        }
    }

    blog_warn("[source] show failed: source '%s' not found", obs_name.c_str());
    return false;
}

bool SourceManager::hide(const std::string &obs_name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto toggle_cb = [](void *param, obs_source_t *source) -> bool {
        const char *target = static_cast<const char *>(param);
        const char *name = obs_source_get_name(source);
        if (name && std::strcmp(name, target) == 0) {
            obs_source_set_enabled(source, false);
            return false;
        }
        return true;
    };

    obs_enum_sources(toggle_cb, const_cast<char *>(obs_name.c_str()));

    for (auto &src : sources_) {
        if (src.obs_name == obs_name) {
            src.showing = false;
            blog_info("[source] hide '%s'", obs_name.c_str());
            return true;
        }
    }

    blog_warn("[source] hide failed: source '%s' not found", obs_name.c_str());
    return false;
}

nlohmann::json SourceManager::list_json()
{
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json arr = nlohmann::json::array();

    for (const auto &src : sources_) {
        nlohmann::json item;
        item["id"]        = src.id;
        item["obsName"]   = src.obs_name;
        item["alias"]     = src.alias;
        item["type"]      = src.type;
        item["category"]  = src.category;
        item["active"]    = src.active;
        item["showing"]   = src.showing;
        item["width"]     = src.width;
        item["height"]    = src.height;
        item["fps"]       = src.fps;
        item["audioLevel"] = src.audio_level;
        item["muted"]     = src.muted;
        item["colorTag"]  = src.color_tag;
        item["sortOrder"] = src.sort_order;

        arr.push_back(std::move(item));
    }

    return arr;
}

}  // namespace multicam
