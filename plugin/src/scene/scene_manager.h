#pragma once

#include <string>
#include <vector>
#include <mutex>

#include <nlohmann/json.hpp>

namespace multicam {

struct SceneInfo {
    std::string name;
    std::vector<std::string> sources;
    std::string layout_type;   // fullscreen, pip, split, triple, quad
    bool is_builtin = false;
};

class SceneManager {
public:
    bool scan();  // 扫描 OBS 当前场景列表
    std::vector<SceneInfo> list();
    bool switch_to(const std::string &scene_name);
    bool create_scene(const std::string &scene_name, const std::string &layout_type,
                      const std::vector<std::string> &source_names);
    bool delete_scene(const std::string &scene_name);
    bool add_source(const std::string &scene_name, const std::string &obs_name);
    bool remove_source(const std::string &scene_name, const std::string &obs_name);
    std::string current_scene_name();
    nlohmann::json list_json();

private:
    std::vector<SceneInfo> scenes_;
    std::mutex mutex_;
    void sync_builtin_scenes();
};

} // namespace multicam
