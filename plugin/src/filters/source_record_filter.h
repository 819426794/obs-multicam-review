#pragma once
#include <obs-module.h>
#include <string>
#include <atomic>

namespace multicam {

/**
 * SourceRecordFilter — OBS 滤镜，放在源上捕获独立视频帧并编码为文件
 *
 * 通过 filter_video/filter_audio 回调获取源的原始帧（未经场景合成），
 * 送入独立的 video_output_t 管线 → 专用 encoder → ffmpeg_muxer → 独立 mp4 文件。
 */
struct SourceRecordFilter {
    obs_source_t *self = nullptr;       // 滤镜自身 (obs_source_t)
    obs_source_t *parent = nullptr;     // 被滤镜的源

    // 独立录制管线
    obs_encoder_t *video_encoder = nullptr;
    obs_encoder_t *audio_encoder = nullptr;
    obs_output_t  *output         = nullptr;
    video_t       *video_output   = nullptr;  // 独立视频管线（隔离于主管线）

    std::string output_path;
    std::string source_name;
    int video_width  = 0;
    int video_height = 0;

    std::atomic<bool> recording{false};
};

// ── 模块注册/注销 ──
void register_source_record_filter();
void unregister_source_record_filter();

// ── 便捷 API ──

/**
 * 在源上创建录制滤镜并启动独立录制。
 * @return 滤镜数据指针（失败返回 nullptr）
 */
SourceRecordFilter *source_record_filter_start(
    obs_source_t   *source,
    const std::string &output_path,
    int width, int height);

/**
 * 停止录制并从源上移除滤镜。
 * 滤镜内部的编码器、输出、视频管线都会被销毁。
 */
void source_record_filter_stop(SourceRecordFilter *filter);

} // namespace multicam
