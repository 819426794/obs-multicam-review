#include "recorder.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace multicam {

namespace {

// Generate a timestamp-based recording id: "rec_YYYYMMDD_HHMMSS"
std::string generate_recording_id() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "rec_%Y%m%d_%H%M%S", &tm_now);
    return std::string(buf);
}

// Current time in seconds since epoch
double now_sec() {
    auto now = std::chrono::system_clock::now();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count()) / 1e9;
}

} // anonymous namespace

bool Recorder::start(const std::string &output_dir, const std::string &recording_id,
                     bool record_independent_sources) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != RecState::Idle) {
        if (state_ == RecState::Recording) {
            blog_warn("[recorder] Already recording (id: %s), ignoring start", recording_id_.c_str());
        } else {
            blog_warn("[recorder] Currently paused (id: %s), stop first before starting new recording",
                      recording_id_.c_str());
        }
        return false;
    }

    // Validate: OBS must be running (startup completed)
    if (!obs_initialized()) {
        blog_error("[recorder] OBS not initialized, cannot start recording");
        return false;
    }

    // Validate output directory
    if (output_dir.empty()) {
        blog_error("[recorder] Output directory is empty");
        return false;
    }

    // Create output directory (recursive)
    int mkdir_result = os_mkdirs(output_dir.c_str());
    if (mkdir_result < 0) {
        blog_error("[recorder] Failed to create output directory: %s", output_dir.c_str());
        return false;
    }

    // Check if directory is writable
    std::error_code ec;
    auto test_path = std::filesystem::path(output_dir) / ".rec_writable_test";
    {
        std::ofstream test_file(test_path);
        if (!test_file) {
            blog_error("[recorder] Output directory not writable: %s", output_dir.c_str());
            return false;
        }
    }
    std::filesystem::remove(test_path, ec);

    // Determine recording id
    std::string rec_id = recording_id.empty() ? generate_recording_id() : recording_id;

    // Independent source recording — not yet implemented
    if (record_independent_sources) {
        blog_info("[recorder] Independent source recording requested — not yet implemented (TODO)");
    }

    // Start PGM recording via OBS frontend API
    obs_frontend_recording_start();

    state_ = RecState::Recording;
    output_dir_ = output_dir;
    recording_id_ = rec_id;
    start_time_ = now_sec();
    files_.clear();

    blog_info("[recorder] Recording started: %s (dir: %s)", rec_id.c_str(), output_dir.c_str());
    return true;
}

bool Recorder::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == RecState::Idle) {
        blog_warn("[recorder] Not recording, ignoring stop request");
        return false;
    }

    std::string rec_id = recording_id_;

    // Stop recording via frontend API
    obs_frontend_recording_stop();

    // Scan output directory for recorded files
    files_.clear();
    if (!output_dir_.empty()) {
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(output_dir_, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            RecFile rf;
            rf.path = entry.path().string();
            rf.size_bytes = entry.file_size(ec);
            // Source name is derived from filename stem for simplicity;
            // exact source-to-file mapping requires OBS output naming config.
            rf.source_name = entry.path().stem().string();
            files_.push_back(rf);
        }
    }

    state_ = RecState::Idle;

    blog_info("[recorder] Recording stopped: %s (%zu files)", rec_id.c_str(), files_.size());
    return true;
}

bool Recorder::pause() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != RecState::Recording) {
        blog_warn("[recorder] Not in recording state (state=%d), ignoring pause",
                  static_cast<int>(state_));
        return false;
    }

    obs_frontend_recording_pause(true);
    state_ = RecState::Paused;

    blog_info("[recorder] Recording paused: %s", recording_id_.c_str());
    return true;
}

bool Recorder::resume() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != RecState::Paused) {
        blog_warn("[recorder] Not paused (state=%d), ignoring resume",
                  static_cast<int>(state_));
        return false;
    }

    obs_frontend_recording_pause(false);
    state_ = RecState::Recording;

    blog_info("[recorder] Recording resumed: %s", recording_id_.c_str());
    return true;
}

RecStatus Recorder::status() {
    std::lock_guard<std::mutex> lock(mutex_);

    RecStatus s;
    s.state = state_;
    s.recording_id = recording_id_;
    s.files = files_;

    if (state_ == RecState::Recording || state_ == RecState::Paused) {
        // Duration since recording started
        s.duration_sec = now_sec() - start_time_;

        // Disk free space on output volume
        if (!output_dir_.empty()) {
            s.disk_free_bytes = os_get_free_disk_space(output_dir_.c_str());
        }

        // Get output statistics from the active recording output
        obs_output_t *output = obs_frontend_get_recording_output();
        if (output) {
            s.total_bytes = obs_output_get_total_bytes(output);
            s.dropped_frames = obs_output_get_frames_dropped(output);
            obs_output_release(output);
        }
    }

    return s;
}

nlohmann::json Recorder::status_json() {
    RecStatus s = status();

    nlohmann::json j;
    switch (s.state) {
        case RecState::Idle:      j["state"] = "idle";      break;
        case RecState::Recording: j["state"] = "recording"; break;
        case RecState::Paused:    j["state"] = "paused";    break;
    }
    j["recordingId"]   = s.recording_id;
    j["durationSec"]   = s.duration_sec;
    j["totalBytes"]    = s.total_bytes;
    j["droppedFrames"] = s.dropped_frames;
    j["diskFreeBytes"] = s.disk_free_bytes;

    nlohmann::json files_arr = nlohmann::json::array();
    for (const auto &f : s.files) {
        nlohmann::json fj;
        fj["sourceName"] = f.source_name;
        fj["path"]       = f.path;
        fj["sizeBytes"]  = f.size_bytes;
        files_arr.push_back(fj);
    }
    j["files"] = files_arr;

    return j;
}

// Private stubs — reserved for future independent-source recording support

bool Recorder::create_output_for_source(const std::string &source_name) {
    blog_info("[recorder] create_output_for_source '%s' — not yet implemented (TODO)",
              source_name.c_str());
    return false;
}

bool Recorder::start_all_outputs() {
    blog_info("[recorder] start_all_outputs — not yet implemented (TODO)");
    return false;
}

bool Recorder::stop_all_outputs() {
    blog_info("[recorder] stop_all_outputs — not yet implemented (TODO)");
    return false;
}

} // namespace multicam
