#include "preset/preset_manager.h"

#include <obs-module.h>
#include <util/platform.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <ctime>
#include <cstring>

#define blog_info(fmt, ...)  blog(LOG_INFO,    "[preset] " fmt, ##__VA_ARGS__)
#define blog_warn(fmt, ...)  blog(LOG_WARNING, "[preset] " fmt, ##__VA_ARGS__)
#define blog_error(fmt, ...) blog(LOG_ERROR,   "[preset] " fmt, ##__VA_ARGS__)

namespace fs = std::filesystem;

// ──────────────────────────────────────────────
//  公共接口
// ──────────────────────────────────────────────

bool PresetManager::init(const char *preset_dir) {
    if (!preset_dir || !preset_dir[0]) {
        blog_error("init: empty preset directory");
        return false;
    }

    preset_dir_ = preset_dir;

    // 确保目录存在
    std::error_code ec;
    fs::create_directories(preset_dir_, ec);
    if (ec) {
        blog_error("Failed to create preset directory: %s (%s)",
                   preset_dir_.c_str(), ec.message().c_str());
        return false;
    }

    scan_directory();
    blog_info("Preset manager initialized: %s (%zu presets)",
              preset_dir_.c_str(), presets_.size());
    return true;
}

std::vector<PresetManager::PresetInfo> PresetManager::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory();
    return presets_;
}

bool PresetManager::save(const std::string &name, const std::string &description,
                         const nlohmann::json &config) {
    if (name.empty()) {
        blog_error("save: empty preset name");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::string path = preset_path(name);
    nlohmann::json doc;
    doc["name"] = name;
    doc["description"] = description;
    doc["config"] = config;

    // 时间戳
    time_t now = time(nullptr);
    char time_buf[64];
    struct tm tm_buf;
    gmtime_s(&tm_buf, &now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    doc["saved_at"] = time_buf;

    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        blog_error("save: Cannot open file %s", path.c_str());
        return false;
    }
    ofs << doc.dump(2);
    ofs.close();

    scan_directory();
    blog_info("Preset saved: %s", path.c_str());
    return true;
}

bool PresetManager::load(const std::string &preset_name) {
    if (preset_name.empty()) {
        blog_error("load: empty preset name");
        return false;
    }

    std::string path = preset_path(preset_name);

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        blog_error("load: Preset file not found: %s", path.c_str());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    try {
        auto doc = nlohmann::json::parse(content);
        blog_info("Preset loaded: %s (keys in config: %zu)",
                  preset_name.c_str(),
                  doc.value("config", nlohmann::json::object()).size());
        return true;
    } catch (const nlohmann::json::parse_error &e) {
        blog_error("load: JSON parse error: %s", e.what());
        return false;
    }
}

bool PresetManager::remove(const std::string &preset_name) {
    if (preset_name.empty()) {
        blog_error("remove: empty preset name");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::string path = preset_path(preset_name);
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        blog_error("remove: Failed to delete %s (%s)",
                   path.c_str(), ec.message().c_str());
        return false;
    }

    scan_directory();
    blog_info("Preset removed: %s", preset_name.c_str());
    return true;
}

nlohmann::json PresetManager::list_json() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto &p : presets_) {
        nlohmann::json item;
        item["id"] = p.id;
        item["name"] = p.name;
        item["description"] = p.description;
        item["createdAt"] = p.created_at;
        item["updatedAt"] = p.updated_at;
        arr.push_back(item);
    }

    nlohmann::json body;
    body["presets"] = arr;
    return body;
}

// ──────────────────────────────────────────────
//  内部方法
// ──────────────────────────────────────────────

std::string PresetManager::preset_path(const std::string &name) const {
    // 安全检查：拒绝包含路径分隔符的名称
    if (name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos) {
        return "";
    }
    return (fs::path(preset_dir_) / (name + ".json")).string();
}

void PresetManager::scan_directory() {
    presets_.clear();

    std::error_code ec;
    if (!fs::exists(preset_dir_, ec)) return;

    for (const auto &entry : fs::directory_iterator(preset_dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        PresetInfo info;
        info.id = entry.path().stem().string();
        info.name = info.id;

        // 尝试读取文件获取元数据
        std::ifstream ifs(entry.path());
        if (ifs.is_open()) {
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            ifs.close();
            try {
                auto doc = nlohmann::json::parse(content);
                info.name = doc.value("name", info.id);
                info.description = doc.value("description", "");
                info.created_at = doc.value("saved_at", "");
                info.updated_at = doc.value("saved_at", "");
            } catch (...) {
                // 使用默认值
            }
        }

        presets_.push_back(info);
    }
}
