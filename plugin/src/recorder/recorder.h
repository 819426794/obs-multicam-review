#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace multicam {

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

    bool create_output_for_source(const std::string &source_name);
    bool start_all_outputs();
    bool stop_all_outputs();
};

} // namespace multicam
