#include "scene_manager.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace multicam {

namespace {

// 回调：遍历场景内所有 source item，收集名称
bool collect_source_names(obs_scene_t * /*scene*/, obs_sceneitem_t *item, void *param) {
    auto *names = static_cast<std::vector<std::string> *>(param);
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (src) {
        names->push_back(obs_source_get_name(src));
    }
    return true; // continue enumeration
}

// 内置场景定义
struct BuiltinDef {
    const char *name;
    const char *layout_type;
};

const BuiltinDef kBuiltins[] = {
    {"全屏预览", "fullscreen"},
    {"画中画",   "pip"},
    {"两分屏",   "split"},
    {"三分屏",   "triple"},
    {"四分屏",   "quad"},
};

} // anonymous namespace

// ───────────────────── scan ─────────────────────

bool SceneManager::scan() {
    std::lock_guard<std::mutex> lock(mutex_);

    scenes_.clear();

    obs_frontend_source_list scene_list = {};
    obs_frontend_get_scenes(&scene_list);

    for (size_t i = 0; i < scene_list.sources_enum; ++i) {
        obs_source_t *scene_source = scene_list.sources[i];
        if (!scene_source)
            continue;

        obs_scene_t *scene = obs_scene_from_source(scene_source);
        if (!scene)
            continue;

        SceneInfo info;
        info.name = obs_source_get_name(scene_source);

        // 枚举场景内的所有源
        obs_scene_enum_items(scene, collect_source_names, &info.sources);

        scenes_.push_back(std::move(info));
    }

    obs_frontend_source_list_free(&scene_list);

    blog_info("[scene] scanned %zu scenes", scenes_.size());
    return true;
}

// ───────────────────── list ─────────────────────

std::vector<SceneInfo> SceneManager::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    return scenes_;
}

// ───────────────────── switch_to ─────────────────────

bool SceneManager::switch_to(const std::string &scene_name) {
    obs_source_t *scene_source = obs_get_source_by_name(scene_name.c_str());
    if (!scene_source) {
        blog_warn("[scene] switch_to: scene '%s' not found", scene_name.c_str());
        return false;
    }

    obs_frontend_set_current_scene(scene_source);
    obs_source_release(scene_source);

    blog_info("[scene] switched to '%s'", scene_name.c_str());
    return true;
}

// ───────────────────── create_scene ─────────────────────

bool SceneManager::create_scene(const std::string &scene_name,
                                const std::string &layout_type,
                                const std::vector<std::string> &source_names) {
    // 检查重名
    obs_source_t *existing = obs_get_source_by_name(scene_name.c_str());
    if (existing) {
        obs_source_release(existing);
        blog_warn("[scene] create_scene: '%s' already exists", scene_name.c_str());
        return false;
    }

    obs_scene_t *scene = obs_scene_create(scene_name.c_str());
    if (!scene) {
        blog_error("[scene] create_scene: failed to create '%s'", scene_name.c_str());
        return false;
    }

    // 添加源到场景
    for (const auto &src_name : source_names) {
        obs_source_t *src = obs_get_source_by_name(src_name.c_str());
        if (src) {
            obs_sceneitem_t *item = obs_scene_add(scene, src);
            if (item) {
                obs_sceneitem_release(item);
            }
            obs_source_release(src);
        } else {
            blog_warn("[scene] create_scene: source '%s' not found, skipping", src_name.c_str());
        }
    }

    blog_info("[scene] created scene '%s' (layout=%s, sources=%zu)",
              scene_name.c_str(), layout_type.c_str(), source_names.size());

    // 刷新列表，标记 layout_type
    scan();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &s : scenes_) {
            if (s.name == scene_name) {
                s.layout_type = layout_type;
                break;
            }
        }
    }

    return true;
}

// ───────────────────── delete_scene ─────────────────────

bool SceneManager::delete_scene(const std::string &scene_name) {
    // 禁止删除内置场景
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(scenes_.begin(), scenes_.end(),
                               [&](const SceneInfo &s) {
                                   return s.name == scene_name && s.is_builtin;
                               });
        if (it != scenes_.end()) {
            blog_warn("[scene] delete_scene: cannot delete builtin scene '%s'",
                      scene_name.c_str());
            return false;
        }
    }

    obs_source_t *scene_source = obs_get_source_by_name(scene_name.c_str());
    if (!scene_source) {
        blog_warn("[scene] delete_scene: scene '%s' not found", scene_name.c_str());
        return false;
    }

    obs_scene_t *scene = obs_scene_from_source(scene_source);
    if (!scene) {
        obs_source_release(scene_source);
        blog_error("[scene] delete_scene: '%s' is not a valid scene", scene_name.c_str());
        return false;
    }

    obs_scene_release(scene);
    obs_source_release(scene_source);

    blog_info("[scene] deleted scene '%s'", scene_name.c_str());

    scan();
    return true;
}

// ───────────────────── add_source ─────────────────────

bool SceneManager::add_source(const std::string &scene_name, const std::string &obs_name) {
    obs_source_t *scene_source = obs_get_source_by_name(scene_name.c_str());
    if (!scene_source) {
        blog_warn("[scene] add_source: scene '%s' not found", scene_name.c_str());
        return false;
    }

    obs_scene_t *scene = obs_scene_from_source(scene_source);
    if (!scene) {
        obs_source_release(scene_source);
        blog_error("[scene] add_source: '%s' is not a valid scene", scene_name.c_str());
        return false;
    }

    obs_source_t *src = obs_get_source_by_name(obs_name.c_str());
    if (!src) {
        obs_source_release(scene_source);
        blog_warn("[scene] add_source: source '%s' not found", obs_name.c_str());
        return false;
    }

    obs_sceneitem_t *item = obs_scene_add(scene, src);
    if (item) {
        obs_sceneitem_release(item);
    }

    obs_source_release(src);
    obs_source_release(scene_source);

    if (!item) {
        blog_error("[scene] add_source: failed to add '%s' to '%s'",
                   obs_name.c_str(), scene_name.c_str());
        return false;
    }

    blog_info("[scene] added source '%s' to scene '%s'", obs_name.c_str(), scene_name.c_str());
    scan();
    return true;
}

// ───────────────────── remove_source ─────────────────────

bool SceneManager::remove_source(const std::string &scene_name, const std::string &obs_name) {
    obs_source_t *scene_source = obs_get_source_by_name(scene_name.c_str());
    if (!scene_source) {
        blog_warn("[scene] remove_source: scene '%s' not found", scene_name.c_str());
        return false;
    }

    obs_scene_t *scene = obs_scene_from_source(scene_source);
    if (!scene) {
        obs_source_release(scene_source);
        blog_error("[scene] remove_source: '%s' is not a valid scene", scene_name.c_str());
        return false;
    }

    obs_sceneitem_t *item = obs_scene_find_source(scene, obs_name.c_str());
    if (!item) {
        obs_source_release(scene_source);
        blog_warn("[scene] remove_source: source '%s' not in scene '%s'",
                  obs_name.c_str(), scene_name.c_str());
        return false;
    }

    obs_sceneitem_remove(item);

    obs_source_release(scene_source);

    blog_info("[scene] removed source '%s' from scene '%s'", obs_name.c_str(), scene_name.c_str());
    scan();
    return true;
}

// ───────────────────── current_scene_name ─────────────────────

std::string SceneManager::current_scene_name() {
    obs_source_t *current = obs_frontend_get_current_scene();
    if (!current)
        return "";

    const char *name = obs_source_get_name(current);
    std::string result = name ? name : "";
    obs_source_release(current);
    return result;
}

// ───────────────────── list_json ─────────────────────

nlohmann::json SceneManager::list_json() {
    nlohmann::json j;
    j["currentScene"] = current_scene_name();

    nlohmann::json arr = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &s : scenes_) {
            nlohmann::json sj;
            sj["name"] = s.name;
            sj["sources"] = s.sources;
            sj["layoutType"] = s.layout_type;
            sj["isBuiltin"] = s.is_builtin;
            arr.push_back(std::move(sj));
        }
    }
    j["scenes"] = std::move(arr);

    return j;
}

// ───────────────────── sync_builtin_scenes ─────────────────────

void SceneManager::sync_builtin_scenes() {
    // 确保每个内置场景都存在
    for (const auto &def : kBuiltins) {
        obs_source_t *existing = obs_get_source_by_name(def.name);
        if (!existing) {
            obs_scene_t *scene = obs_scene_create(def.name);
            if (scene) {
                blog_info("[scene] created builtin scene '%s' (layout=%s)",
                          def.name, def.layout_type);
            } else {
                blog_error("[scene] failed to create builtin scene '%s'", def.name);
            }
        } else {
            obs_source_release(existing);
        }
    }

    // 重新扫描并标记内置场景
    scan();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &s : scenes_) {
            for (const auto &def : kBuiltins) {
                if (s.name == def.name) {
                    s.is_builtin = true;
                    s.layout_type = def.layout_type;
                    break;
                }
            }
        }
    }
}

} // namespace multicam
