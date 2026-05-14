#pragma once
#include <obs-module.h>

#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace multicam {

struct SourceRecordFilter;  // forward declaration

enum class RecState { Idle, Recording, Paused };

struct RecFile {
    std::string source_name;
    std::string path;
    int64_t size_bytes = 0;
};

struct RecStatus {
    RecState state = RecState::Idle;
    std::string recording_id;
    double duration_sec = 0.0;
    int64_t total_bytes = 0;
    int dropped_frames = 0;
    int64_t disk_free_bytes = 0;
    std::vector<RecFile> files;
};

class Recorder {
public:
    bool start(const std::string &output_dir, const std::string &recording_id,
               bool record_independent_sources = true);
    bool stop();
    bool pause();
    bool resume();
    RecStatus status();
    nlohmann::json status_json();

private:
    RecState state_ = RecState::Idle;
    std::string output_dir_;
    std::string recording_id_;
    double start_time_ = 0.0;
    std::vector<RecFile> files_;
    std::mutex mutex_;

    // Per-source independent recording — 通过 SourceRecordFilter 实现隔离
    struct SourceOutput {
        SourceRecordFilter *filter = nullptr;  // 滤镜指针（管理编码器+输出）
        std::string         source_name;
        std::string         file_path;
    };
    std::vector<SourceOutput> source_outputs_;

    // Source-aware recording outputs
    bool create_output_for_source(const std::string &source_name);
    bool start_all_outputs();
    bool stop_all_outputs();

    // Collect names of all active video/audio sources in the current scene
    std::vector<std::string> collect_active_sources();

    // Video encoder factory (kept for potential PGM recording or future use)
    obs_encoder_t *create_video_encoder(const std::string &name);
    // Audio encoder factory: AAC @ 320kbps
    obs_encoder_t *create_audio_encoder(const std::string &name);
};

} // namespace multicam
