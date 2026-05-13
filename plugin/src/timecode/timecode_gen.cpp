#include "timecode_gen.h"

#include <obs-module.h>
#include <util/platform.h>

#include <cmath>
#include <iomanip>
#include <sstream>

namespace multicam {

// ============================================================================
// Public API
// ============================================================================

void TimecodeGen::reset(double start_time_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    start_sec_ = start_time_sec;
    current_sec_ = start_time_sec;
    blog_info("[timecode] reset to %.3f sec", start_time_sec);
}

void TimecodeGen::set_framerate(FrameRate fr) {
    std::lock_guard<std::mutex> lock(mutex_);
    fps_ = fr;
    int fps_val = 0;
    switch (fr) {
        case FrameRate::FPS_24: fps_val = 24; break;
        case FrameRate::FPS_25: fps_val = 25; break;
        case FrameRate::FPS_30: fps_val = 30; break;
        case FrameRate::FPS_60: fps_val = 60; break;
    }
    blog_info("[timecode] framerate set to %d fps", fps_val);
}

void TimecodeGen::set_drop_frame(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    drop_frame_ = enabled;
    blog_info("[timecode] drop-frame mode %s", enabled ? "enabled (29.97 NTSC)" : "disabled");
}

void TimecodeGen::tick(double current_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_sec_ = current_sec;
}

bool TimecodeGen::is_drop_frame() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return drop_frame_;
}

std::string TimecodeGen::smpte() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SmpteFields f = compute_smpte_unlocked();
    return format_smpte(f.negative ? -f.hours : f.hours, f.mins, f.secs, f.frames, drop_frame_);
}

double TimecodeGen::total_seconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_sec_ - start_sec_;
}

int TimecodeGen::frame_number() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double elapsed = current_sec_ - start_sec_;
    double fps_real = drop_frame_ ? (30000.0 / 1001.0) : static_cast<double>(frames_per_second());
    return static_cast<int>(std::round(elapsed * fps_real));
}

nlohmann::json TimecodeGen::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double elapsed = current_sec_ - start_sec_;
    double fps_real = drop_frame_ ? (30000.0 / 1001.0) : static_cast<double>(frames_per_second());
    int fn = static_cast<int>(std::round(elapsed * fps_real));

    SmpteFields f = compute_smpte_unlocked();
    std::string tc = format_smpte(f.negative ? -f.hours : f.hours, f.mins, f.secs, f.frames, drop_frame_);

    return {
        {"smpte", tc},
        {"seconds", elapsed},
        {"frameNumber", fn}
    };
}

// ============================================================================
// Private helpers
// ============================================================================

TimecodeGen::SmpteFields TimecodeGen::compute_smpte_unlocked() const {
    double elapsed = current_sec_ - start_sec_;
    double fps_nominal = static_cast<double>(frames_per_second());
    double fps_real = drop_frame_ ? (30000.0 / 1001.0) : fps_nominal;

    // Total frames based on real elapsed time
    int total_frames = static_cast<int>(std::round(elapsed * fps_real));
    bool negative = total_frames < 0;
    if (negative) total_frames = -total_frames;

    // Drop-frame SMPTE: add back dropped frame numbers
    // Frame numbers 00 and 01 are skipped at the start of every minute
    // except minutes 0, 10, 20, 30, 40, 50 (every 10th minute)
    if (drop_frame_) {
        int fps = static_cast<int>(fps_nominal);
        int display_minutes = total_frames / (fps * 60);
        int drop_count = display_minutes - display_minutes / 10;
        total_frames += drop_count * 2;
    }

    int fps = static_cast<int>(fps_nominal);
    int frames = total_frames % fps;
    int total_secs = total_frames / fps;
    int secs = total_secs % 60;
    int total_mins = total_secs / 60;
    int mins = total_mins % 60;
    int hours = total_mins / 60;

    return {hours, mins, secs, frames, negative};
}

std::string TimecodeGen::format_smpte(int hours, int mins, int secs, int frames, bool drop) const {
    std::ostringstream oss;
    if (hours < 0) {
        oss << '-';
        hours = -hours;
    }
    oss << std::setfill('0') << std::setw(2) << hours << ':'
        << std::setw(2) << mins << ':'
        << std::setw(2) << secs;
    // Drop-frame uses semicolon before frames: "01:23:45;15"
    oss << (drop ? ';' : ':');
    oss << std::setw(2) << frames;
    return oss.str();
}

} // namespace multicam
