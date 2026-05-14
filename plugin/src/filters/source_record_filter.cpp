#include "source_record_filter.h"
#include <obs-module.h>
#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace multicam {

static const char *FILTER_ID = "multicam_source_record";

// ═══════════════════════════════════════════════════════════════════════════
// 内部辅助 — 编码器工厂
// ═══════════════════════════════════════════════════════════════════════════

/**
 * 创建视频编码器（硬件优先，回退到 x264）
 * 编码器 ID 选择与 Recorder 保持一致
 */
static obs_encoder_t *create_video_encoder_internal(const std::string &name) {
    const char *encoder_ids[] = {
        "obs_nvenc_h264",       // NVIDIA NVENC (OBS native)
        "obs_qsv11_h264",       // Intel QSV
        "amd_amf_h264",         // AMD AMF
        "ffmpeg_nvenc",         // FFmpeg NVENC fallback
        "obs_x264",             // Software x264
    };

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "rate_control", "CQP");
    obs_data_set_int(settings, "cqp", 20);                 // CQ level 20: high quality
    obs_data_set_string(settings, "profile", "high");
    obs_data_set_string(settings, "preset", "p5");         // P5: good quality (NVENC)

    for (const char *enc_id : encoder_ids) {
        obs_encoder_t *enc = obs_video_encoder_create(
            enc_id, name.c_str(), settings, nullptr);
        if (enc) {
            blog_info("[source-record-filter] Using video encoder '%s' for '%s'",
                      enc_id, name.c_str());
            obs_data_release(settings);
            return enc;
        }
    }

    obs_data_release(settings);
    blog_error("[source-record-filter] No video encoder available for '%s'",
               name.c_str());
    return nullptr;
}

/**
 * 创建音频编码器 (AAC 320kbps, 48kHz stereo)
 */
static obs_encoder_t *create_audio_encoder_internal(const std::string &name) {
    obs_data_t *settings = obs_data_create();
    obs_data_set_int(settings, "bitrate", 320);
    obs_data_set_string(settings, "rate_control", "CBR");

    obs_encoder_t *enc = obs_audio_encoder_create(
        "ffmpeg_aac", name.c_str(), settings, 0, nullptr);
    obs_data_release(settings);

    if (!enc) {
        blog_error("[source-record-filter] Failed to create AAC encoder for '%s'",
                   name.c_str());
    }
    return enc;
}

// ═══════════════════════════════════════════════════════════════════════════
// 内部辅助 — 独立管线管理
// ═══════════════════════════════════════════════════════════════════════════

/**
 * 创建专用 video_output_t（独立于 OBS 主管线）。
 *
 * 此管线仅用于视频编码器的格式初始化（分辨率、帧率、色彩空间）。
 * 不通过 pipeline 推送帧 — 所有帧通过 filter_video 回调中
 * 的 obs_encoder_encode_video() 直接喂入编码器。
 */
static bool create_dedicated_video_output(SourceRecordFilter *f,
                                          int width, int height) {
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        blog_error("[source-record-filter] Cannot get OBS video info");
        return false;
    }

    video_output_info vi = {};
    vi.format     = VIDEO_FORMAT_NV12;
    vi.width      = (width  > 0) ? (uint32_t)width  : ovi.base_width;
    vi.height     = (height > 0) ? (uint32_t)height : ovi.base_height;
    vi.fps_num    = ovi.fps_num;
    vi.fps_den    = ovi.fps_den;
    vi.cache_size = 16;
    vi.colorspace = VIDEO_CS_709;
    vi.range      = VIDEO_RANGE_PARTIAL;
    vi.name       = "multicam_src_record";

    int ret = video_output_open(&f->video_output, &vi);
    if (ret != VIDEO_OUTPUT_SUCCESS) {
        blog_error("[source-record-filter] video_output_open failed: %d", ret);
        return false;
    }

    blog_info("[source-record-filter] Dedicated video_output: %dx%d @ %d/%d fps",
              vi.width, vi.height, vi.fps_num, vi.fps_den);
    return true;
}

/**
 * 开始内部录制：创建独立 video_output → encoder → ffmpeg_muxer 管线
 */
static bool start_recording_internal(SourceRecordFilter *f) {
    if (!f || f->recording.load()) return false;

    // ── 1. 创建独立视频管线（仅用于 encoder 参数初始化） ──
    if (!create_dedicated_video_output(f, f->video_width, f->video_height)) {
        return false;
    }

    // ── 2. 创建视频编码器，连接至独立管线 ──
    std::string venc_name = "src_venc_" + f->source_name;
    f->video_encoder = create_video_encoder_internal(venc_name);
    if (!f->video_encoder) {
        video_output_close(f->video_output);
        f->video_output = nullptr;
        return false;
    }
    obs_encoder_set_video(f->video_encoder, f->video_output);

    // ── 3. 创建音频编码器 ──
    std::string aenc_name = "src_aenc_" + f->source_name;
    f->audio_encoder = create_audio_encoder_internal(aenc_name);
    if (f->audio_encoder) {
        obs_encoder_set_audio(f->audio_encoder, obs_get_audio());
    }

    // ── 4. 创建 ffmpeg_muxer 输出 ──
    obs_data_t *out_settings = obs_data_create();
    obs_data_set_string(out_settings, "path", f->output_path.c_str());

    std::string out_name = "src_out_" + f->source_name;
    f->output = obs_output_create("ffmpeg_muxer", out_name.c_str(),
                                   out_settings, nullptr);
    obs_data_release(out_settings);

    if (!f->output) {
        blog_error("[source-record-filter] Failed to create ffmpeg_muxer output");
        if (f->video_encoder) { obs_encoder_release(f->video_encoder); f->video_encoder = nullptr; }
        if (f->audio_encoder) { obs_encoder_release(f->audio_encoder); f->audio_encoder = nullptr; }
        video_output_close(f->video_output);
        f->video_output = nullptr;
        return false;
    }

    // ── 5. 绑定编码器到输出 ──
    obs_output_set_video_encoder(f->output, f->video_encoder);
    if (f->audio_encoder) {
        obs_output_set_audio_encoder(f->output, f->audio_encoder, 0);
    }

    // ── 6. 启动输出 ──
    if (!obs_output_start(f->output)) {
        const char *err = obs_output_get_last_error(f->output);
        blog_error("[source-record-filter] Failed to start output: %s",
                   err ? err : "unknown error");
        obs_output_release(f->output);
        f->output = nullptr;
        if (f->video_encoder) { obs_encoder_release(f->video_encoder); f->video_encoder = nullptr; }
        if (f->audio_encoder) { obs_encoder_release(f->audio_encoder); f->audio_encoder = nullptr; }
        video_output_close(f->video_output);
        f->video_output = nullptr;
        return false;
    }

    f->recording.store(true);
    blog_info("[source-record-filter] Recording started: '%s' → %s",
              f->source_name.c_str(), f->output_path.c_str());
    return true;
}

/**
 * 停止内部录制：关闭 output → 释放 encoder → 关闭 video_output
 */
static void stop_recording_internal(SourceRecordFilter *f) {
    if (!f || !f->recording.load()) return;

    f->recording.store(false);

    // 先停输出（其内部会等待编码器完成编码）
    if (f->output) {
        blog_info("[source-record-filter] Stopping output for '%s'",
                  f->source_name.c_str());
        obs_output_stop(f->output);
        obs_output_release(f->output);
        f->output = nullptr;
    }

    // 释放编码器
    if (f->video_encoder) {
        obs_encoder_release(f->video_encoder);
        f->video_encoder = nullptr;
    }
    if (f->audio_encoder) {
        obs_encoder_release(f->audio_encoder);
        f->audio_encoder = nullptr;
    }

    // 关闭独立视频管线
    if (f->video_output) {
        video_output_close(f->video_output);
        f->video_output = nullptr;
    }

    blog_info("[source-record-filter] Recording stopped: '%s'",
              f->source_name.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════
// OBS 滤镜回调
// ═══════════════════════════════════════════════════════════════════════════

static const char *filter_get_name(void * /*type_data*/) {
    return obs_module_text("MulticamSourceRecord");
}

static void *filter_create(obs_data_t *settings, obs_source_t *source) {
    auto *f = new SourceRecordFilter();
    f->self   = source;                        // 滤镜自身
    f->parent = obs_filter_get_parent(source); // 被滤镜的源

    f->output_path  = obs_data_get_string(settings, "output_path");
    f->video_width  = (int)obs_data_get_int(settings, "video_width");
    f->video_height = (int)obs_data_get_int(settings, "video_height");

    if (f->parent) {
        f->source_name = obs_source_get_name(f->parent);
    }

    blog_info("[source-record-filter] Filter created for '%s'", f->source_name.c_str());
    return f;
}

static void filter_destroy(void *data) {
    auto *f = static_cast<SourceRecordFilter *>(data);
    if (f) {
        stop_recording_internal(f);
        blog_info("[source-record-filter] Filter destroyed for '%s'",
                  f->source_name.c_str());
        delete f;
    }
}

static obs_properties_t *filter_properties(void * /*data*/) {
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "output_path",
        obs_module_text("OutputPath"), OBS_TEXT_DEFAULT);
    obs_properties_add_int(props, "video_width",
        obs_module_text("Width"), 1, 7680, 1);
    obs_properties_add_int(props, "video_height",
        obs_module_text("Height"), 1, 4320, 1);
    return props;
}

/**
 * filter_video — 核心回调。
 *
 * 在每个视频帧渲染时（主管线渲染线程）被调用。
 * 通过 obs_encoder_encode_video() 直接将该帧喂入独立编码器。
 *
 * 编码器连接的是专用 video_output_t（无 pipeline 帧），
 * 因此录制的内容是源原始画面，而非主管线 PGM 合成。
 */
static obs_source_frame *filter_video(void *data, obs_source_frame *frame) {
    auto *f = static_cast<SourceRecordFilter *>(data);

    if (!f->recording.load() || !f->video_encoder || !frame) {
        return frame; // 直通，不修改主管线
    }

    // 直接将该帧送入编码器 — OBS 内部会拷贝帧数据
    obs_encoder_encode_video(f->video_encoder, frame);

    return frame; // 直通主管线
}

/**
 * filter_audio — 音频回调。
 *
 * 将源音频帧直接送入音频编码器。
 */
static obs_audio_data *filter_audio(void *data, obs_audio_data *audio) {
    auto *f = static_cast<SourceRecordFilter *>(data);

    if (!f->recording.load() || !f->audio_encoder || !audio) {
        return audio; // 直通
    }

    obs_encoder_encode_audio(f->audio_encoder, audio);

    return audio; // 直通
}

// ═══════════════════════════════════════════════════════════════════════════
// 滤镜注册信息
// ═══════════════════════════════════════════════════════════════════════════

static obs_source_info source_record_filter_info = {
    .id           = FILTER_ID,
    .type         = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC,

    .get_name       = filter_get_name,
    .create         = filter_create,
    .destroy        = filter_destroy,
    .get_properties = filter_properties,
    .filter_video   = filter_video,
    .filter_audio   = filter_audio,
};

void register_source_record_filter() {
    obs_register_source(&source_record_filter_info);
    blog_info("[source-record-filter] Registered '%s' filter", FILTER_ID);
}

void unregister_source_record_filter() {
    blog_info("[source-record-filter] Unregistered '%s' filter", FILTER_ID);
}

// ═══════════════════════════════════════════════════════════════════════════
// 便捷 API
// ═══════════════════════════════════════════════════════════════════════════

SourceRecordFilter *source_record_filter_start(
    obs_source_t *source,
    const std::string &output_path,
    int width, int height)
{
    if (!source) {
        blog_error("[source-record-filter] source_record_filter_start: null source");
        return nullptr;
    }

    std::string src_name = obs_source_get_name(source);

    // ── 创建滤镜实例（内部调用 filter_create） ──
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "output_path", output_path.c_str());
    obs_data_set_int(settings, "video_width",  width);
    obs_data_set_int(settings, "video_height", height);

    std::string filter_name = "Record: " + src_name;
    obs_source_t *filter_source = obs_source_create_private(
        FILTER_ID, filter_name.c_str(), settings);
    obs_data_release(settings);

    if (!filter_source) {
        blog_error("[source-record-filter] Failed to create filter source for '%s'",
                   src_name.c_str());
        return nullptr;
    }

    // ── 获取滤镜内部的 C++ 数据指针 ──
    auto *f = static_cast<SourceRecordFilter *>(
        obs_obj_get_data(filter_source));
    if (!f) {
        blog_error("[source-record-filter] Failed to get filter data for '%s'",
                   src_name.c_str());
        obs_source_release(filter_source);
        return nullptr;
    }

    // ── 确保关键字段正确 ──
    f->source_name = src_name;
    f->output_path = output_path;
    if (width  > 0) f->video_width  = width;
    if (height > 0) f->video_height = height;

    // ── 将滤镜挂载到源上 ──
    obs_source_filter_add(source, filter_source);

    // ── 启动内部录制管线（创建 encoder + output） ──
    if (!start_recording_internal(f)) {
        blog_error("[source-record-filter] Failed to start recording for '%s'",
                   src_name.c_str());
        obs_source_filter_remove(source, filter_source);
        obs_source_release(filter_source);
        return nullptr;
    }

    // filter_source 现在由父源持有引用（obs_source_filter_add 加了引用）。
    // 释放我们自己的创建引用，避免泄漏。
    obs_source_release(filter_source);

    blog_info("[source-record-filter] SourceRecordFilter started on '%s' → %s",
              src_name.c_str(), output_path.c_str());
    return f;
}

void source_record_filter_stop(SourceRecordFilter *filter) {
    if (!filter) return;

    // 1. 停止内部录制（停止 output、释放 encoder、关闭 video_output）
    stop_recording_internal(filter);

    // 2. 从父源上移除滤镜（会自动释放滤镜并调用 filter_destroy）
    if (filter->parent && filter->self) {
        obs_source_filter_remove(filter->parent, filter->self);
    }
}

} // namespace multicam
