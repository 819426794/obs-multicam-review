#include "audio/audio_manager.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace multicam {

// ──────────────────────────────────────────────
//  常量
// ──────────────────────────────────────────────

static constexpr double kSilenceDB      = -120.0;
static constexpr double kSignalThreshold = -50.0;   // > -50 dB 认为有信号
static constexpr int    kMaxTracks       = 6;
static constexpr double kMinVolume       = 0.0;
static constexpr double kMaxVolume       = 1.0;

// ──────────────────────────────────────────────
//  内部工具
// ──────────────────────────────────────────────

/// 将 OBS 0.0-1.0 balance 转换为 -1.0-1.0 pan
static inline double balance_to_pan(float balance_value)
{
    return static_cast<double>(balance_value) * 2.0 - 1.0;
}

/// 将 -1.0-1.0 pan 转换为 OBS 0.0-1.0 balance
static inline float pan_to_balance(double pan)
{
    return static_cast<float>((pan + 1.0) / 2.0);
}

/// 线性音量 0.0-1.0 → dB (0.001 以上有效，否则静音)
static inline double volume_to_db(double vol)
{
    if (vol <= 0.0001)
        return kSilenceDB;
    return 20.0 * std::log10(vol);
}

/// 校验 track_mask: 只保留 Track 1-6 的位
static inline int clamp_track_mask(int mask)
{
    return mask & 0x3F;  // 0x3F = 0b111111
}

// ──────────────────────────────────────────────
//  refresh_channels 回调
// ──────────────────────────────────────────────

struct RefreshParam {
    std::vector<AudioChannel> *channels;
    std::string *solo_source_name;
};

static bool refresh_enum_callback(void *param, obs_source_t *source)
{
    auto *rp = static_cast<RefreshParam *>(param);
    if (!source || !rp || !rp->channels)
        return true;

    const char *obs_id = obs_source_get_id(source);
    if (!obs_id)
        return true;

    // 筛选音频源类型
    bool is_audio = false;
    if (std::strcmp(obs_id, "wasapi_input_capture") == 0
        || std::strcmp(obs_id, "wasapi_output_capture") == 0
        || std::strcmp(obs_id, "pulse_input_capture") == 0
        || std::strcmp(obs_id, "pulse_output_capture") == 0
        || std::strcmp(obs_id, "coreaudio_input_capture") == 0
        || std::strcmp(obs_id, "coreaudio_output_capture") == 0
        || std::strcmp(obs_id, "jack_input_capture") == 0)
    {
        is_audio = true;
    }
    else if (std::strcmp(obs_id, "dshow_input") == 0
             || std::strcmp(obs_id, "v4l2_input") == 0
             || std::strcmp(obs_id, "decklink-input") == 0)
    {
        // 视频捕获设备 — 仅当附带音频流时纳入
        uint32_t flags = obs_source_get_output_flags(source);
        is_audio = (flags & OBS_SOURCE_AUDIO) != 0;
    }

    if (!is_audio)
        return true;

    // 跳过场景
    if (std::strcmp(obs_id, "scene") == 0)
        return true;

    obs_source_t *ref = obs_source_get_ref(source);
    if (!ref)
        return true;

    const char *name = obs_source_get_name(source);

    AudioChannel ch;
    ch.source_name = name ? name : "";

    // 读取音量 (0.0-1.0 线性倍率)
    float vol = obs_source_get_volume(source);
    ch.volume = std::clamp(static_cast<double>(vol), kMinVolume, 1.5);  // 允许 boost 到 150%
    ch.db = volume_to_db(ch.volume);

    ch.muted = obs_source_muted(source);

    // Pan — 从 balance 转换
    // obs_source_get_balance_value 可能不可用，默认居中
    ch.pan = 0.0;

    ch.vu_level = kSilenceDB;
    ch.signal_present = false;
    ch.solo = false;
    ch.alias = "";

    rp->channels->push_back(std::move(ch));
    obs_source_release(ref);

    blog_info("[audio] refresh found source: '%s' (type=%s)",
              name ? name : "?", obs_id);

    return true;
}

// ──────────────────────────────────────────────
//  AudioManager 实现
// ──────────────────────────────────────────────

bool AudioManager::is_audio_source(const char *obs_id, obs_source_t *source)
{
    if (!obs_id)
        return false;

    if (std::strcmp(obs_id, "wasapi_input_capture") == 0
        || std::strcmp(obs_id, "wasapi_output_capture") == 0
        || std::strcmp(obs_id, "pulse_input_capture") == 0
        || std::strcmp(obs_id, "pulse_output_capture") == 0
        || std::strcmp(obs_id, "coreaudio_input_capture") == 0
        || std::strcmp(obs_id, "coreaudio_output_capture") == 0
        || std::strcmp(obs_id, "jack_input_capture") == 0)
    {
        return true;
    }

    // 视频设备附带音频流
    if (std::strcmp(obs_id, "dshow_input") == 0
        || std::strcmp(obs_id, "v4l2_input") == 0
        || std::strcmp(obs_id, "decklink-input") == 0)
    {
        uint32_t flags = obs_source_get_output_flags(source);
        return (flags & OBS_SOURCE_AUDIO) != 0;
    }

    return false;
}

void AudioManager::refresh_channels()
{
    blog_info("[audio] refreshing channel list...");

    channels_.clear();

    RefreshParam rp;
    rp.channels = &channels_;
    rp.solo_source_name = &solo_source_name_;

    obs_enum_sources(refresh_enum_callback, &rp);

    // 如果当前有 Solo 源，更新其状态
    if (!solo_source_name_.empty()) {
        for (auto &ch : channels_) {
            if (ch.source_name == solo_source_name_) {
                ch.solo = true;
                break;
            }
        }
    }

    blog_info("[audio] refresh complete: %zu audio channel(s)", channels_.size());
}

void AudioManager::release_solo()
{
    if (solo_source_name_.empty())
        return;

    blog_info("[audio] releasing solo from '%s'", solo_source_name_.c_str());

    // 恢复 Solo 前各源的 mute 状态
    for (const auto &[name, was_muted] : pre_solo_mutes_) {
        obs_source_t *src = obs_get_source_by_name(name.c_str());
        if (src) {
            obs_source_set_muted(src, was_muted);
            obs_source_release(src);
        }
        // 同步内部状态
        for (auto &ch : channels_) {
            if (ch.source_name == name) {
                ch.muted = was_muted;
                break;
            }
        }
    }

    // 重置自身
    for (auto &ch : channels_) {
        if (ch.source_name == solo_source_name_) {
            ch.solo = false;
            break;
        }
    }

    solo_source_name_.clear();
    pre_solo_mutes_.clear();
}

void AudioManager::apply_solo(const std::string &source_name)
{
    blog_info("[audio] applying solo to '%s'", source_name.c_str());

    pre_solo_mutes_.clear();

    for (auto &ch : channels_) {
        if (ch.source_name == source_name) {
            ch.solo = true;
            ch.muted = false;
            // OBS 层面：取消 mute
            obs_source_t *src = obs_get_source_by_name(ch.source_name.c_str());
            if (src) {
                obs_source_set_muted(src, false);
                obs_source_release(src);
            }
        }
        else {
            // 记录 Solo 前的 mute 状态
            pre_solo_mutes_[ch.source_name] = ch.muted;
            ch.muted = true;
            // OBS 层面：强制 mute
            obs_source_t *src = obs_get_source_by_name(ch.source_name.c_str());
            if (src) {
                obs_source_set_muted(src, true);
                obs_source_release(src);
            }
        }
    }

    solo_source_name_ = source_name;
}

// ──────────────────────────────────────────────
//  Public API
// ──────────────────────────────────────────────

void AudioManager::tick()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 若 channels_ 为空则先刷新一次
    if (channels_.empty()) {
        refresh_channels();
    }

    for (auto &ch : channels_) {
        obs_source_t *src = obs_get_source_by_name(ch.source_name.c_str());
        if (!src) {
            ch.vu_level = kSilenceDB;
            ch.signal_present = false;
            continue;
        }

        // 创建临时 volmeter 读取实时电平
        obs_volmeter_t *vm = obs_volmeter_create(OBS_FADER_LOG);
        if (!vm) {
            obs_source_release(src);
            continue;
        }

        obs_volmeter_attach_source(vm, src);

        // 给 volmeter 一小段稳定时间… 在 50ms tick 内这已是最快路径
        float cur_db = obs_volmeter_get_cur_db(vm);
        // 钳位最小值
        if (cur_db < static_cast<float>(kSilenceDB))
            cur_db = static_cast<float>(kSilenceDB);

        ch.vu_level = static_cast<double>(cur_db);
        ch.signal_present = (ch.vu_level > kSignalThreshold);

        obs_volmeter_detach_source(vm);
        obs_volmeter_destroy(vm);
        obs_source_release(src);
    }
}

std::vector<AudioChannel> AudioManager::channels()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_;
}

bool AudioManager::set_volume(const std::string &source_name, double volume)
{
    std::lock_guard<std::mutex> lock(mutex_);

    double clamped = std::clamp(volume, kMinVolume, 1.5);  // 允许 150% boost

    // 操作 OBS 源
    obs_source_t *src = obs_get_source_by_name(source_name.c_str());
    if (!src) {
        blog_warn("[audio] set_volume failed: '%s' not found", source_name.c_str());
        return false;
    }
    obs_source_set_volume(src, static_cast<float>(clamped));
    obs_source_release(src);

    // 更新内部状态
    for (auto &ch : channels_) {
        if (ch.source_name == source_name) {
            ch.volume = clamped;
            ch.db = volume_to_db(clamped);
            blog_info("[audio] set_volume '%s' → %.3f (%.1f dB)",
                      source_name.c_str(), clamped, ch.db);
            return true;
        }
    }

    blog_warn("[audio] set_volume internal update: '%s' not in channels_",
              source_name.c_str());
    return false;
}

bool AudioManager::set_mute(const std::string &source_name, bool muted)
{
    std::lock_guard<std::mutex> lock(mutex_);

    obs_source_t *src = obs_get_source_by_name(source_name.c_str());
    if (!src) {
        blog_warn("[audio] set_mute failed: '%s' not found", source_name.c_str());
        return false;
    }
    obs_source_set_muted(src, muted);
    obs_source_release(src);

    for (auto &ch : channels_) {
        if (ch.source_name == source_name) {
            ch.muted = muted;
            blog_info("[audio] set_mute '%s' → %s",
                      source_name.c_str(), muted ? "ON" : "OFF");
            return true;
        }
    }

    blog_warn("[audio] set_mute internal update: '%s' not in channels_",
              source_name.c_str());
    return false;
}

bool AudioManager::set_solo(const std::string &source_name, bool solo)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!solo) {
        // 取消 Solo —
        // 仅当 source_name 确实是当前 Solo 源时才执行
        if (solo_source_name_ == source_name) {
            release_solo();
            blog_info("[audio] solo OFF for '%s'", source_name.c_str());
        }
        else {
            // 请求取消的不是当前 Solo 源，忽略
            blog_warn("[audio] set_solo(false) ignored: '%s' is not the solo source",
                      source_name.c_str());
        }
        return true;
    }

    // 请求开启 Solo
    // 若已有其他 Solo 源，先释放
    if (!solo_source_name_.empty() && solo_source_name_ != source_name) {
        release_solo();
    }

    // 若已是当前 Solo 源，不重复处理
    if (solo_source_name_ == source_name) {
        blog_info("[audio] solo already active on '%s'", source_name.c_str());
        return true;
    }

    // 确保 channels_ 中有该源
    bool found = false;
    for (const auto &ch : channels_) {
        if (ch.source_name == source_name) {
            found = true;
            break;
        }
    }
    if (!found) {
        // 刷新一次再试
        refresh_channels();
        found = false;
        for (const auto &ch : channels_) {
            if (ch.source_name == source_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            blog_warn("[audio] set_solo failed: '%s' not found in channels",
                      source_name.c_str());
            return false;
        }
    }

    apply_solo(source_name);
    blog_info("[audio] solo ON for '%s'", source_name.c_str());
    return true;
}

bool AudioManager::set_pan(const std::string &source_name, double pan)
{
    std::lock_guard<std::mutex> lock(mutex_);

    double clamped = std::clamp(pan, -1.0, 1.0);

    obs_source_t *src = obs_get_source_by_name(source_name.c_str());
    if (!src) {
        blog_warn("[audio] set_pan failed: '%s' not found", source_name.c_str());
        return false;
    }
    obs_source_set_balance_value(src, pan_to_balance(clamped));
    obs_source_release(src);

    for (auto &ch : channels_) {
        if (ch.source_name == source_name) {
            ch.pan = clamped;
            blog_info("[audio] set_pan '%s' → %.2f", source_name.c_str(), clamped);
            return true;
        }
    }

    blog_warn("[audio] set_pan internal update: '%s' not in channels_",
              source_name.c_str());
    return false;
}

double AudioManager::master_volume()
{
    std::lock_guard<std::mutex> lock(mutex_);

    float vol = obs_get_master_volume();
    master_vol_ = static_cast<double>(vol);
    return master_vol_;
}

bool AudioManager::set_master_volume(double vol)
{
    std::lock_guard<std::mutex> lock(mutex_);

    double clamped = std::clamp(vol, kMinVolume, kMaxVolume);
    obs_set_master_volume(static_cast<float>(clamped));
    master_vol_ = clamped;

    blog_info("[audio] master volume → %.3f", clamped);
    return true;
}

nlohmann::json AudioManager::channels_json()
{
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json arr = nlohmann::json::array();

    for (const auto &ch : channels_) {
        nlohmann::json item;
        item["sourceName"]    = ch.source_name;
        item["alias"]         = ch.alias;
        item["volume"]        = ch.volume;
        item["dB"]            = ch.db;
        item["muted"]         = ch.muted;
        item["solo"]          = ch.solo;
        item["pan"]           = ch.pan;
        item["vuLevel"]       = ch.vu_level;
        item["signalPresent"] = ch.signal_present;
        item["vuPercent"]     = (ch.vu_level <= kSilenceDB) ? 0.0
                                : std::clamp((ch.vu_level / -kSilenceDB) * 100.0 + 100.0,
                                             0.0, 100.0);

        arr.push_back(std::move(item));
    }

    return arr;
}

// ──────────────────────────────────────────────
//  音频路由
// ──────────────────────────────────────────────

bool AudioManager::set_track_routing(const std::string &source_name, int track_mask)
{
    std::lock_guard<std::mutex> lock(mutex_);

    int mask = clamp_track_mask(track_mask);
    if (mask == 0) {
        blog_warn("[audio] set_track_routing: empty mask for '%s'", source_name.c_str());
    }

    obs_source_t *src = obs_get_source_by_name(source_name.c_str());
    if (!src) {
        blog_warn("[audio] set_track_routing failed: '%s' not found",
                  source_name.c_str());
        return false;
    }

    obs_source_set_audio_mixers(src, static_cast<uint32_t>(mask));
    obs_source_release(src);

    blog_info("[audio] track routing '%s' → 0x%02X", source_name.c_str(), mask);
    return true;
}

int AudioManager::get_track_routing(const std::string &source_name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    obs_source_t *src = obs_get_source_by_name(source_name.c_str());
    if (!src) {
        blog_warn("[audio] get_track_routing failed: '%s' not found",
                  source_name.c_str());
        return 0;
    }

    uint32_t mask = obs_source_get_audio_mixers(src);
    obs_source_release(src);

    return static_cast<int>(mask & 0x3F);
}

} // namespace multicam
