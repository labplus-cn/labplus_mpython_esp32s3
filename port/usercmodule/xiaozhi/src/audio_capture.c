/*
 * audio_capture.c - GMF Capture pipeline 封装实现（含 AFE 唤醒词检测）
 *
 * pipeline 数据流（AFE 模式，有 SR 模型时）：
 *   硬件 stereo PCM (16kHz, 16-bit, 2ch)
 *     → [ai_afe]   AFE 处理（立体声→单声道 + 降噪 + WakeNet 唤醒词检测）
 *     → [aud_enc]  Opus 编码 (16kHz mono, 60ms 帧, 32kbps VBR, VOIP 模式)
 *     → Opus 帧（供调用方通过 acquire/release_frame 获取）
 *
 * 回退 pipeline（无 SR 模型或未设置 wakeup_cb 时，输入输出采样率相同）：
 *   硬件 stereo PCM (16kHz, 16-bit, 2ch)
 *     → [aud_ch_cvt]  stereo → mono
 *     → [aud_enc]     Opus 编码
 *     → Opus 帧
 *
 * 前置条件：调用 record_pipe_open() 前，音频硬件必须已初始化。
 */

#include "audio_capture.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"

/* Opus 编码器配置 */
#include "esp_audio_types.h"
#include "esp_audio_enc.h"
#include "esp_opus_enc.h"

/* GMF Capture */
#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_capture_audio_dev_src.h"
#include "esp_capture_advance.h"
#include "esp_gmf_audio_enc.h"

/* GMF AFE 元素（唤醒词检测 + 音频前端处理） */
#include "esp_gmf_afe.h"
#include "esp_gmf_afe_manager.h"

/* ESP-SR 模型加载 */
#include "model_path.h"       /* srmodel_list_t, esp_srmodel_init/deinit */
#include "esp_afe_sr_models.h" /* esp_afe_handle_from_config */

#define TAG "rec_pipe"

/* 音频参数 */
#define OPUS_SAMPLE_RATE   16000
#define OPUS_CHANNELS      1        /* mono 输出 */
#define OPUS_FRAME_MS      60
#define OPUS_BITRATE       32000
#define REC_BITS           16

/* AFE 任务栈大小（默认 3KB 可能不足，增大到 4KB） */
#define AFE_FEED_TASK_STACK    (4 * 1024)
#define AFE_FETCH_TASK_STACK   (4 * 1024)

/* ====================================================
 * 内部结构
 * ==================================================== */

struct record_pipe_s {
    /* GMF Capture */
    esp_capture_audio_src_if_t *audio_src;
    esp_capture_handle_t        capture;
    esp_capture_sink_handle_t   capture_sink;
    esp_capture_stream_frame_t  cur_frame;
    bool                        frame_acquired;

    /* AFE（可选：有 SR 模型且 wakeup_cb 非 NULL 时启用） */
    srmodel_list_t              *models;      /* SR 模型列表，需在 afe_manager 销毁后释放 */
    afe_config_t                *afe_cfg;     /* AFE 配置，需手动释放 */
    esp_gmf_afe_manager_handle_t afe_manager; /* AFE Manager（生命周期由此结构管理） */

    /* 唤醒词回调 */
    record_pipe_wakeup_cb_t     wakeup_cb;
    void                        *wakeup_ctx;

    /* 唤醒词结束回调 */
    record_pipe_wakeup_end_cb_t wakeup_end_cb;
    void                        *wakeup_end_ctx;
};

/* ====================================================
 * 内部：AFE 事件回调（在 AFE fetch 任务中调用）
 * ==================================================== */

static void afe_event_cb(esp_gmf_element_handle_t el,
                         esp_gmf_afe_evt_t *event,
                         void *user_data)
{
    struct record_pipe_s *rp = (struct record_pipe_s *)user_data;
    switch (event->type) {
    case ESP_GMF_AFE_EVT_WAKEUP_START:
        ESP_LOGI(TAG, "Wake word detected!");
        if (rp->wakeup_cb) {
            rp->wakeup_cb(rp->wakeup_ctx);
        }
        break;
    case ESP_GMF_AFE_EVT_WAKEUP_END:
        ESP_LOGI(TAG, "Wakeup end (timeout, no speech)");
        if (rp->wakeup_end_cb) {
            rp->wakeup_end_cb(rp->wakeup_end_ctx);
        }
        break;
    case ESP_GMF_AFE_EVT_VAD_START:
        ESP_LOGD(TAG, "VAD: speech start");
        break;
    case ESP_GMF_AFE_EVT_VAD_END:
        ESP_LOGD(TAG, "VAD: speech stop");
        break;
    default:
        break;
    }
}

/* ====================================================
 * 内部：pipeline 任务栈大小调度
 *   aenc_0 是 Opus 编码任务，esp-gmf 建议 Opus 需要 40KB 栈。
 * ==================================================== */

static void pipeline_thread_scheduler(const char *name,
                                       esp_capture_thread_schedule_cfg_t *cfg)
{
    if (name && strcmp(name, "aenc_0") == 0) {
        cfg->stack_size = 40 * 1024;
    }
}

/* ====================================================
 * 内部：pipeline 构建完成后配置 Opus 编码器参数
 * ==================================================== */

static esp_capture_err_t pipeline_event_cb(esp_capture_event_t event, void *ctx)
{
    if (event != ESP_CAPTURE_EVENT_AUDIO_PIPELINE_BUILT) {
        return ESP_CAPTURE_ERR_OK;
    }

    esp_capture_sink_handle_t sink = (esp_capture_sink_handle_t)ctx;
    esp_gmf_element_handle_t enc_el = NULL;
    esp_capture_sink_get_element_by_tag(sink, ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                        "aud_enc", &enc_el);
    if (!enc_el) {
        ESP_LOGW(TAG, "pipeline_event_cb: aud_enc not found");
        return ESP_CAPTURE_ERR_OK;
    }

    esp_opus_enc_config_t opus_cfg = {
        .sample_rate      = OPUS_SAMPLE_RATE,
        .channel          = ESP_AUDIO_MONO,
        .bits_per_sample  = ESP_AUDIO_BIT16,
        .bitrate          = OPUS_BITRATE,
        .frame_duration   = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP,
        .complexity       = 0,
        .enable_fec       = false,
        .enable_dtx       = false,
        .enable_vbr       = true,
    };
    esp_audio_enc_config_t enc_cfg = {
        .type   = ESP_AUDIO_TYPE_OPUS,
        .cfg    = &opus_cfg,
        .cfg_sz = sizeof(opus_cfg),
    };
    esp_gmf_err_t err = esp_gmf_audio_enc_reconfig(enc_el, &enc_cfg);
    if (err != ESP_GMF_ERR_OK) {
        ESP_LOGW(TAG, "Opus enc reconfig failed: %d (will use defaults)", err);
    } else {
        ESP_LOGI(TAG, "Opus enc: %dms VOIP %dbps VBR", OPUS_FRAME_MS, OPUS_BITRATE);
    }
    return ESP_CAPTURE_ERR_OK;
}

/* ====================================================
 * 公开 API
 * ==================================================== */

esp_err_t record_pipe_open(esp_codec_dev_handle_t rec_dev,
                            record_pipe_wakeup_cb_t wakeup_cb, void *wakeup_ctx,
                            record_pipe_wakeup_end_cb_t wakeup_end_cb, void *wakeup_end_ctx,
                            record_pipe_handle_t *out_rp)
{
    if (!rec_dev || !out_rp) return ESP_ERR_INVALID_ARG;

    /* 注册 Opus 编码器（幂等，可安全多次调用） */
    esp_opus_enc_register();

    /* 为 Opus 编码任务注册栈大小调度器（默认 4KB 不足，增大到 40KB） */
    esp_capture_set_thread_scheduler(pipeline_thread_scheduler);

    struct record_pipe_s *rp = calloc(1, sizeof(struct record_pipe_s));
    if (!rp) return ESP_ERR_NO_MEM;

    rp->wakeup_cb      = wakeup_cb;
    rp->wakeup_ctx     = wakeup_ctx;
    rp->wakeup_end_cb  = wakeup_end_cb;
    rp->wakeup_end_ctx = wakeup_end_ctx;

    /* 音频设备源 */
    esp_capture_audio_dev_src_cfg_t src_cfg = {
        .record_handle = rec_dev,
    };
    rp->audio_src = esp_capture_new_audio_dev_src(&src_cfg);
    if (!rp->audio_src) {
        ESP_LOGE(TAG, "esp_capture_new_audio_dev_src failed");
        free(rp);
        return ESP_FAIL;
    }

    esp_capture_audio_info_t fixed = {
        .format_id      = ESP_CAPTURE_FMT_ID_PCM,  // 源只支持 PCM，必须填 PCM
        .sample_rate    = 16000,
        .channel        = 2,
        .bits_per_sample = 16,
    };
    rp->audio_src->set_fixed_caps(rp->audio_src, &fixed);   // ← 关键调用

    /* Capture 实例 */
    esp_capture_cfg_t cap_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_NONE,
        .audio_src = rp->audio_src,
        .video_src = NULL,
    };
    esp_capture_err_t cerr = esp_capture_open(&cap_cfg, &rp->capture);
    if (cerr != ESP_CAPTURE_ERR_OK || !rp->capture) {
        ESP_LOGE(TAG, "esp_capture_open failed: %d", cerr);
        free(rp->audio_src);
        free(rp);
        return ESP_FAIL;
    }

    /* ---- AFE 初始化（可选）---- */
    bool has_afe = false;

    if (wakeup_cb) {
        /* 加载 flash 分区 "model" 中的 SR 模型 */
        rp->models = esp_srmodel_init("model");
        if (!rp->models || rp->models->num <= 0) {
            ESP_LOGW(TAG, "SR models not found - wakeup detection disabled");
            rp->models = NULL;
        }
    }

    if (rp->models) {
        /* AFE 配置：SR 模式（唤醒词检测），输入格式 "MM"（双声道麦克风，无参考信号） */
        rp->afe_cfg = afe_config_init("M", rp->models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF); //AFE_MODE_HIGH_PERF); AFE_MODE_LOW_COST
        if (!rp->afe_cfg) {
            ESP_LOGW(TAG, "afe_config_init failed - wakeup detection disabled");
            esp_srmodel_deinit(rp->models);
            rp->models = NULL;
        }
    }

    if (rp->afe_cfg) {
        /* AFE Manager（read_cb/result_cb 由 esp_gmf_afe 元素在 open 时自动设置） */
        rp->afe_cfg->aec_init = false;
        rp->afe_cfg->pcm_config.total_ch_num = 2;
        rp->afe_cfg->pcm_config.mic_num = 1;  /* 单麦模式：关闭 SE(BSS)，降低 FETCH 算力，缓解 FEED is full */
        rp->afe_cfg->pcm_config.ref_num = 0;
        rp->afe_cfg->pcm_config.sample_rate = 16000;
        rp->afe_cfg->afe_ringbuf_size = 50;  /* 增大 ring buffer，缓解下游处理不及时时的 FEED is full 警告 */
        rp->afe_cfg->vad_init = true;         /* 启用 VAD，使 AFE 内部唤醒/VAD 状态机生效：
                                               * 待机时 VAD OFF → 唤醒词后自动开 VAD → 语音结束后自动关 VAD */
        esp_gmf_afe_manager_cfg_t mgr_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(
            rp->afe_cfg, NULL, NULL, NULL, NULL);
        mgr_cfg.feed_task_setting.stack_size  = AFE_FEED_TASK_STACK;
        mgr_cfg.fetch_task_setting.stack_size = AFE_FETCH_TASK_STACK;
        mgr_cfg.fetch_task_setting.prio = 15;
        if (esp_gmf_afe_manager_create(&mgr_cfg, &rp->afe_manager) != ESP_GMF_ERR_OK) {
            ESP_LOGW(TAG, "afe_manager_create failed - wakeup detection disabled");
            free(rp->afe_cfg);
            rp->afe_cfg = NULL;
            esp_srmodel_deinit(rp->models);
            rp->models = NULL;
        }
    }

    if (rp->afe_manager) {
        /* GMF AFE 元素：包装 AFE Manager，输出 mono 16kHz PCM */
        esp_gmf_afe_cfg_t afe_el_cfg = DEFAULT_GMF_AFE_CFG(
            rp->afe_manager, afe_event_cb, rp, NULL);

        esp_gmf_obj_handle_t afe_obj = NULL;
        if (esp_gmf_afe_init(&afe_el_cfg, &afe_obj) != ESP_GMF_ERR_OK) {
            ESP_LOGW(TAG, "esp_gmf_afe_init failed - wakeup detection disabled");
            esp_gmf_afe_manager_destroy(rp->afe_manager);
            rp->afe_manager = NULL;
            free(rp->afe_cfg);
            rp->afe_cfg = NULL;
            esp_srmodel_deinit(rp->models);
            rp->models = NULL;
        } else {
            /* 注册到 capture 内部元素池（capture 接管元素所有权） */
            cerr = esp_capture_register_element(
                rp->capture, ESP_CAPTURE_STREAM_TYPE_AUDIO,
                (esp_gmf_element_handle_t)afe_obj);
            if (cerr != ESP_CAPTURE_ERR_OK) {
                ESP_LOGW(TAG, "register afe_el failed: %d - wakeup detection disabled", cerr);
                /* 手动销毁未注册成功的元素 */
                esp_gmf_obj_delete(afe_obj);
                esp_gmf_afe_manager_destroy(rp->afe_manager);
                rp->afe_manager = NULL;
                free(rp->afe_cfg);
                rp->afe_cfg = NULL;
                esp_srmodel_deinit(rp->models);
                rp->models = NULL;
            } else {
                has_afe = true;
            }
        }
    }

    /* ---- Sink 配置：请求 Opus 输出（mono 16kHz） ---- */
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id       = ESP_CAPTURE_FMT_ID_OPUS,
            .sample_rate     = OPUS_SAMPLE_RATE,
            .channel         = OPUS_CHANNELS,
            .bits_per_sample = REC_BITS,
        },
    };
    cerr = esp_capture_sink_setup(rp->capture, 0, &sink_cfg, &rp->capture_sink);
    if (cerr != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "esp_capture_sink_setup failed: %d", cerr);
        goto cleanup_fail;
    }

    /* ---- 构建 pipeline ---- */
    if (has_afe) {
        /* AFE 处理立体声→单声道 + 降噪 + 唤醒词检测，直接输出 mono → Opus */
        const char *elements[] = {"ai_afe", "aud_enc"};
        cerr = esp_capture_sink_build_pipeline(rp->capture_sink,
                                               ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                               elements, 2);
        ESP_LOGI(TAG, "Pipeline: mic(stereo) → [ai_afe] → [aud_enc] → Opus");
    } else {
        /* 回退：手动声道转换 + Opus（输入输出采样率相同，无需采样率转换） */
        const char *elements[] = {"aud_ch_cvt", "aud_enc"};
        cerr = esp_capture_sink_build_pipeline(rp->capture_sink,
                                               ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                               elements, 2);
        ESP_LOGI(TAG, "Pipeline: mic(stereo) → [aud_ch_cvt] → [aud_enc] → Opus");
    }

    if (cerr != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "esp_capture_sink_build_pipeline failed: %d", cerr);
        goto cleanup_fail;
    }

    /* pipeline 构建完成后配置 Opus 编码参数 */
    esp_capture_set_event_cb(rp->capture, pipeline_event_cb, rp->capture_sink);
    esp_capture_sink_enable(rp->capture_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);

    ESP_LOGI(TAG, "open OK (AFE wakeup: %s)", has_afe ? "enabled" : "disabled");
    *out_rp = rp;
    return ESP_OK;

cleanup_fail:
    /* afe_el 已注册到 capture，由 esp_capture_close 负责销毁 */
    if (rp->capture) {
        esp_capture_close(rp->capture);
    }
    /* afe_manager 未被 capture 管理，需手动销毁 */
    if (rp->afe_manager) {
        esp_gmf_afe_manager_destroy(rp->afe_manager);
    }
    if (rp->afe_cfg) {
        free(rp->afe_cfg);
    }
    if (rp->models) {
        esp_srmodel_deinit(rp->models);
    }
    free(rp->audio_src);
    free(rp);
    return ESP_FAIL;
}

esp_err_t record_pipe_start(record_pipe_handle_t rp)
{
    if (!rp) return ESP_ERR_INVALID_ARG;
    esp_capture_err_t cerr = esp_capture_start(rp->capture);
    if (cerr != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "esp_capture_start failed: %d", cerr);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t record_pipe_stop(record_pipe_handle_t rp)
{
    if (!rp || !rp->capture) return ESP_ERR_INVALID_ARG;
    esp_capture_stop(rp->capture);
    return ESP_OK;
}

esp_err_t record_pipe_close(record_pipe_handle_t rp)
{
    if (!rp) return ESP_ERR_INVALID_ARG;

    /* 1. 关闭 capture（会关闭并销毁 pipeline 元素，包括 afe_el） */
    if (rp->capture) {
        esp_capture_close(rp->capture);
        rp->capture = NULL;
    }

    /* 2. 销毁 AFE Manager（afe_el 已销毁，可以安全停止任务） */
    if (rp->afe_manager) {
        esp_gmf_afe_manager_destroy(rp->afe_manager);
        rp->afe_manager = NULL;
    }

    /* 3. 释放 AFE 配置 */
    if (rp->afe_cfg) {
        free(rp->afe_cfg);
        rp->afe_cfg = NULL;
    }

    /* 4. 释放 SR 模型（必须在 afe_manager 销毁后，因为 afe_data 使用模型数据） */
    if (rp->models) {
        esp_srmodel_deinit(rp->models);
        rp->models = NULL;
    }

    free(rp->audio_src);
    free(rp);
    return ESP_OK;
}

esp_err_t record_pipe_acquire_frame(record_pipe_handle_t rp,
                                    const uint8_t **data, int *size)
{
    if (!rp || !data || !size) return ESP_ERR_INVALID_ARG;

    rp->cur_frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
    esp_capture_err_t cerr = esp_capture_sink_acquire_frame(
        rp->capture_sink, &rp->cur_frame, false);

    if (cerr != ESP_CAPTURE_ERR_OK || rp->cur_frame.size <= 0) {
        *data = NULL;
        *size = 0;
        rp->frame_acquired = false;
        return ESP_OK;
    }

    *data = rp->cur_frame.data;
    *size = rp->cur_frame.size;
    rp->frame_acquired = true;
    return ESP_OK;
}

esp_err_t record_pipe_release_frame(record_pipe_handle_t rp)
{
    if (!rp || !rp->frame_acquired) return ESP_ERR_INVALID_ARG;
    esp_capture_sink_release_frame(rp->capture_sink, &rp->cur_frame);
    rp->frame_acquired = false;
    return ESP_OK;
}
