#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace multicam {

struct AudioChannel {
    std::string source_name;   // OBS 源名
    std::string alias;         // 用户别名
    double volume = 1.0;       // 0.0-1.0 (线性)
    double db = 0.0;           // dB 值
    bool muted = false;
    bool solo = false;
    double pan = 0.0;          // -1.0(left) ~ 1.0(right)
    double vu_level = -120.0;  // VU 表电平 dB
    bool signal_present = false;
};

enum class EffectType { Gain, NoiseGate, EQ3Band, Compressor, Limiter };
static const char *effect_type_names[] = {"Gain", "NoiseGate", "EQ3Band", "Compressor", "Limiter"};

struct AudioEffect {
    EffectType type;
    bool enabled = true;
    nlohmann::json params;     // 效果器参数
};

class AudioManager {
public:
    void tick();                // 每 50ms 调用，刷新 VU 表
    std::vector<AudioChannel> channels();
    bool set_volume(const std::string &source_name, double volume);
    bool set_mute(const std::string &source_name, bool muted);
    bool set_solo(const std::string &source_name, bool solo);
    bool set_pan(const std::string &source_name, double pan);
    double master_volume();
    bool set_master_volume(double vol);
    nlohmann::json channels_json();

    // 音频路由矩阵: source → track (1-6)
    bool set_track_routing(const std::string &source_name, int track_mask);
    int get_track_routing(const std::string &source_name);

private:
    std::vector<AudioChannel> channels_;
    double master_vol_ = 1.0;
    std::mutex mutex_;

    // Solo 互斥状态
    std::string solo_source_name_;                          // 当前 Solo 的源名（空 = 无）
    std::unordered_map<std::string, bool> pre_solo_mutes_;  // Solo 前各源的 mute 状态

    void refresh_channels();
    bool is_audio_source(const char *obs_id, obs_source_t *source);
    void release_solo();
    void apply_solo(const std::string &source_name);
};

} // namespace multicam
