#include "recorder.h"
#include "filters/source_record_filter.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <util/platform.h>

#include <nlohmann/json.hpp>

#include <algorithm>
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

    // Start independent source recording (per-source mp4 files)
    if (record_independent_sources) {
        bool ok = start_all_outputs();
        if (!ok) {
            blog_warn("[recorder] Some independent source outputs failed to start");
        }
    }

    // Start PGM recording via OBS frontend API (main program output)
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

    // Stop independent source outputs first
    stop_all_outputs();

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

// ─── Video Encoder Factory ───────────────────────────────────────────────

obs_encoder_t *Recorder::create_video_encoder(const std::string &name) {
    // Priority: NVENC (OBS native) > QSV > AMF > FFmpeg NVENC > x264
    const char *encoder_ids[] = {
        "obs_nvenc_h264",       // NVIDIA NVENC (OBS native, texture-based)
        "obs_qsv11_h264",       // Intel QSV
        "amd_amf_h264",         // AMD AMF
        "ffmpeg_nvenc",         // FFmpeg NVENC fallback
        "obs_x264",             // Software x264
    };

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "rate_control", "CQP");
    obs_data_set_int(settings, "cqp", 20);                 // CQ level 20 (high quality)
    obs_data_set_string(settings, "profile", "high");
    obs_data_set_string(settings, "preset", "p5");         // P5: good quality (NVENC)

    for (const char *enc_id : encoder_ids) {
        obs_encoder_t *enc = obs_video_encoder_create(enc_id, name.c_str(), settings, nullptr);
        if (enc) {
            blog_info("[recorder] Using video encoder '%s' for '%s'", enc_id, name.c_str());
            obs_data_release(settings);
            return enc;
        }
    }

    obs_data_release(settings);
    blog_error("[recorder] No video encoder available for '%s'", name.c_str());
    return nullptr;
}

// ─── Audio Encoder Factory ───────────────────────────────────────────────

obs_encoder_t *Recorder::create_audio_encoder(const std::string &name) {
    // AAC 320kbps, 48kHz stereo
    obs_data_t *settings = obs_data_create();
    obs_data_set_int(settings, "bitrate", 320);
    obs_data_set_string(settings, "rate_control", "CBR");

    obs_encoder_t *enc = obs_audio_encoder_create("ffmpeg_aac", name.c_str(),
                                                   settings, 0, nullptr);
    obs_data_release(settings);

    if (!enc) {
        blog_error("[recorder] Failed to create AAC audio encoder for '%s'", name.c_str());
    }
    return enc;
}

// ─── Active Source Collection ────────────────────────────────────────────

std::vector<std::string> Recorder::collect_active_sources() {
    std::vector<std::string> names;

    if (!obs_initialized()) {
        return names;
    }

    // Collect all sources from the current scene
    obs_source_t *current_scene = obs_frontend_get_current_scene();
    if (!current_scene) {
        blog_warn("[recorder] No current scene; cannot collect sources");
        return names;
    }

    // Iterate scene items
    obs_scene_t *scene = obs_scene_from_source(current_scene);
    if (!scene) {
        obs_source_release(current_scene);
        return names;
    }

    auto enum_proc = [](obs_scene_t * /*scene*/, obs_sceneitem_t *item, void *param) -> bool {
        auto *name_list = static_cast<std::vector<std::string> *>(param);
        obs_source_t *src = obs_sceneitem_get_source(item);
        if (src) {
            const char *name = obs_source_get_name(src);
            if (name && name[0] != '\0') {
                // Skip duplicates
                if (std::find(name_list->begin(), name_list->end(), name) == name_list->end()) {
                    name_list->push_back(name);
                }
            }
        }
        return true; // continue enumeration
    };

    obs_scene_enum_items(scene, enum_proc, &names);
    obs_source_release(current_scene);

    blog_info("[recorder] Found %zu active sources in current scene", names.size());
    return names;
}

// ─── Per-Source Independent Output (via SourceRecordFilter) ───────────────

bool Recorder::create_output_for_source(const std::string &source_name) {
    // 1. Find the OBS source
    obs_source_t *source = obs_get_source_by_name(source_name.c_str());
    if (!source) {
        blog_error("[recorder] Source '%s' not found", source_name.c_str());
        return false;
    }

    // Check source type — we need sources with video capability
    uint32_t source_flags = obs_source_get_output_flags(source);
    bool has_video = (source_flags & OBS_SOURCE_VIDEO) != 0;
    bool has_async_video = (source_flags & OBS_SOURCE_ASYNC_VIDEO) != 0;

    if (!has_video && !has_async_video) {
        blog_info("[recorder] Source '%s' has no video output, skipping", source_name.c_str());
        obs_source_release(source);
        return false;
    }

    // 2. Build output file path
    std::string safe_name = source_name;
    // Replace path-unsafe characters
    for (auto &ch : safe_name) {
        if (ch == ':' || ch == '/' || ch == '\\' || ch == '<' ||
            ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*') {
            ch = '_';
        }
    }
    std::string file_path = output_dir_ + "/" + safe_name + "_" + recording_id_ + ".mp4";

    blog_info("[recorder] Creating independent output for '%s' → %s",
              source_name.c_str(), file_path.c_str());

    // 3. Get source dimensions (use OBS base resolution as fallback)
    int width  = (int)obs_source_get_width(source);
    int height = (int)obs_source_get_height(source);
    if (width <= 0 || height <= 0) {
        obs_video_info ovi;
        if (obs_get_video_info(&ovi)) {
            if (width  <= 0) width  = (int)ovi.base_width;
            if (height <= 0) height = (int)ovi.base_height;
        } else {
            width  = 1920;
            height = 1080;
        }
    }

    // 4. Create SourceRecordFilter on the source — handles encoder+output internally
    //    The filter captures the source's raw video frames (before scene composition)
    //    via a dedicated video_output_t, ensuring true per-source isolation.
    SourceRecordFilter *filter = source_record_filter_start(
        source, file_path, width, height);

    obs_source_release(source);

    if (!filter) {
        blog_error("[recorder] Failed to create source record filter for '%s'",
                   source_name.c_str());
        return false;
    }

    // 5. Track the filter for later cleanup
    SourceOutput so;
    so.filter      = filter;
    so.source_name = source_name;
    so.file_path   = file_path;
    source_outputs_.push_back(so);

    blog_info("[recorder] Independent source recording started for '%s' → %s",
              source_name.c_str(), file_path.c_str());
    return true;
}

// ─── Start / Stop All Independent Outputs ────────────────────────────────

bool Recorder::start_all_outputs() {
    blog_info("[recorder] Starting independent source outputs...");

    auto sources = collect_active_sources();
    if (sources.empty()) {
        blog_warn("[recorder] No active sources found in current scene");
        return false;
    }

    int success_count = 0;
    for (const auto &name : sources) {
        if (create_output_for_source(name)) {
            ++success_count;
        }
    }

    blog_info("[recorder] Started %d/%zu independent outputs",
              success_count, sources.size());
    return success_count > 0;
}

bool Recorder::stop_all_outputs() {
    if (source_outputs_.empty()) {
        return true;
    }

    blog_info("[recorder] Stopping %zu independent source outputs...",
              source_outputs_.size());

    for (auto &so : source_outputs_) {
        if (so.filter) {
            blog_info("[recorder] Stopping source record filter for '%s' (%s)",
                      so.source_name.c_str(), so.file_path.c_str());

            // Stop and remove the filter (releases encoder+output+video_output internally)
            source_record_filter_stop(so.filter);
            so.filter = nullptr;
        }

        // Record the output file in the files_ list for status reporting
        RecFile rf;
        rf.source_name = so.source_name;
        rf.path = so.file_path;

        // Get actual file size from disk
        std::error_code ec;
        if (std::filesystem::exists(so.file_path, ec)) {
            rf.size_bytes = std::filesystem::file_size(so.file_path, ec);
        }
        files_.push_back(rf);
    }

    source_outputs_.clear();

    blog_info("[recorder] All independent outputs stopped");
    return true;
}

} // namespace multicam
