#pragma once

#include <string>
#include <vector>
#include <mutex>

#include <nlohmann/json.hpp>

// PresetManager: 保存/加载插件配置预设
// 定义在全局作用域，匹配 plugin.h 的 forward declaration
class PresetManager {
public:
    struct PresetInfo {
        std::string id;
        std::string name;
        std::string description;
        std::string created_at;
        std::string updated_at;
    };

    bool init(const char *preset_dir);
    std::vector<PresetInfo> list();
    bool save(const std::string &name, const std::string &description,
              const nlohmann::json &config);
    bool load(const std::string &preset_name);
    bool remove(const std::string &preset_name);
    nlohmann::json list_json();

private:
    std::string preset_dir_;
    std::vector<PresetInfo> presets_;
    std::mutex mutex_;

    std::string preset_path(const std::string &name) const;
    void scan_directory();
};
