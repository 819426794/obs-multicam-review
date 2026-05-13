#pragma once

#include <string>
#include <mutex>

#include <nlohmann/json.hpp>

namespace multicam {

// SMPTE timecode format: HH:MM:SS:FF (30fps or 29.97 df)
class TimecodeGen {
public:
    // Frame rate modes
    enum class FrameRate { FPS_24, FPS_25, FPS_30, FPS_60 };

    void reset(double start_time_sec = 0.0);   // Reset/set start point
    void set_framerate(FrameRate fr);
    void set_drop_frame(bool enabled);          // Enable/disable drop-frame (29.97 NTSC)
    void tick(double current_sec);             // Update each frame
    bool is_drop_frame() const;

    // Get formatted timecode string
    std::string smpte() const;                 // "01:23:45:15"
    double total_seconds() const;              // Accumulated seconds
    int frame_number() const;                  // Accumulated frame count

    nlohmann::json to_json() const;            // { smpte, seconds, frameNumber }

private:
    double start_sec_ = 0.0;
    double current_sec_ = 0.0;
    FrameRate fps_ = FrameRate::FPS_30;
    bool drop_frame_ = false;
    mutable std::mutex mutex_;

    int frames_per_second() const {
        switch (fps_) {
            case FrameRate::FPS_24: return 24;
            case FrameRate::FPS_25: return 25;
            case FrameRate::FPS_30: return 30;
            case FrameRate::FPS_60: return 60;
        }
        return 30;
    }

    // Compute SMPTE fields from elapsed seconds (caller must hold lock)
    struct SmpteFields {
        int hours;
        int mins;
        int secs;
        int frames;
        bool negative;
    };
    SmpteFields compute_smpte_unlocked() const;

    std::string format_smpte(int hours, int mins, int secs, int frames, bool drop) const;
};

} // namespace multicam
