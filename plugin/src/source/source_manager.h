#pragma once

#include <string>
#include <vector>
#include <mutex>

#include <nlohmann/json.hpp>

namespace multicam {

struct SourceInfo {
    std::string id;
    std::string obs_name;
    std::string alias;
    std::string type;       // dshow_input, wasapi_input_capture, etc.
    std::string category;   // camera, desktop, window, browser, media, audio, ndi, unknown
    bool active = false;
    bool showing = false;
    int width = 0;
    int height = 0;
    double fps = 0;
    double audio_level = -120.0;  // dB
    bool muted = false;
    std::string color_tag = "#ffffff";
    int sort_order = 0;
};

class SourceManager {
public:
    bool discover();
    std::vector<SourceInfo> list();
    bool rename(const std::string &obs_name, const std::string &alias);
    bool configure(const std::string &obs_name, const std::string &alias,
                   const std::string &color_tag, int sort_order);
    bool show(const std::string &obs_name);
    bool hide(const std::string &obs_name);
    nlohmann::json list_json();

private:
    std::vector<SourceInfo> sources_;
    std::mutex mutex_;
    std::string categorize_source(const std::string &obs_id);
};

}  // namespace multicam
