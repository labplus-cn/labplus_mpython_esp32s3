/*
 * xiaozhi_player.c - 小智AI播放器子系统实现
 *
 * 封装 Opus 解码 + GMF Audio Render + playback_task，
 * 对外只暴露 xiaozhi_player.h 中定义的接口。
 */

#include "xiaozhi_player.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* Opus 解码器 */
#include "esp_audio_types.h"
#include "esp_audio_dec.h"
#include "esp_opus_dec.h"

/* GMF 硬件 */
#include "esp_codec_dev.h"

/* GMF Pool + 格式转换元素 */
#include "esp_gmf_pool.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"

/* GMF Audio Render */
#include "esp_audio_render.h"
#include "esp_audio_render_types.h"

/* ====================================================
 * 内部常量
 * ==================================================== */

#define TAG "xz_player"

/* Opus 解码输出缓冲区（60ms @ 16kHz mono 16-bit，留余量） */
#define OPUS_DEC_OUT_BYTES   4096

/* 播放队列单帧最大字节数 */
#define OPUS_PKT_MAX_BYTES   512

/* 默认播放队列深度 */
#define DEFAULT_QUEUE_SIZE   20

/* playback_task 参数 */
#define PLAYBACK_TASK_STACK  16384
#define PLAYBACK_TASK_PRIO   7

/* 队列排空检测：收到 tts_stop 后，连续 200ms 无新帧则认为播完 */
#define DRAIN_SILENCE_MS     200

/* ====================================================
 * 内部数据结构
 * ==================================================== */

/** 播放队列中的 Opus 数据包 */
typedef struct {
    uint8_t data[OPUS_PKT_MAX_BYTES];
    int     len;
} opus_pkt_t;

/** 播放器上下文（不透明句柄的实体） */
struct xiaozhi_player_s {
    /* 硬件 */
    esp_codec_dev_handle_t play_dev;

    /* 音频格式（来自 config） */
    int sample_rate;
    int channels;
    int bits;
    int frame_ms;

    /* Opus 解码器 */
    void *opus_decoder;

    /* GMF Pool + Audio Render */
    esp_gmf_pool_handle_t             gmf_pool;
    esp_audio_render_handle_t         render;
    esp_audio_render_stream_handle_t  render_stream;

    /* 播放队列 */
    QueueHandle_t play_queue;

    /* 任务句柄 */
    TaskHandle_t task;

    /* TTS 结束标志：收到 tts_stop 后置 true，任务检测队列排空后触发回调 */
    volatile bool tts_stop_received;

    /* 排空回调 */
    xiaozhi_player_drained_cb_t on_drained;
    void                       *on_drained_ctx;
};

/* ====================================================
 * 内部辅助函数
 * ==================================================== */

/** esp_audio_render 输出回调：将 PCM 写入扬声器 */
static int render_write_cb(uint8_t *pcm_data, uint32_t pcm_size, void *ctx)
{
    esp_codec_dev_handle_t dev = (esp_codec_dev_handle_t)ctx;
    esp_codec_dev_write(dev, pcm_data, (int)pcm_size);
    return 0;
}

/**
 * 创建 GMF Pool，注册 ch_cvt / bit_cvt / rate_cvt 元素。
 * esp_audio_render 依赖这些元素做格式自动转换（mono→stereo 等）。
 */
static esp_err_t create_gmf_pool(esp_gmf_pool_handle_t *pool)
{
    *pool = NULL;
    if (esp_gmf_pool_init(pool) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "gmf_pool_init failed");
        return ESP_FAIL;
    }

    esp_gmf_element_handle_t el = NULL;

    esp_ae_ch_cvt_cfg_t ch_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_ch_cvt_init(&ch_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_bit_cvt_cfg_t bit_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    esp_gmf_bit_cvt_init(&bit_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_rate_cvt_cfg_t rate_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    esp_gmf_rate_cvt_init(&rate_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    return ESP_OK;
}

/* ====================================================
 * playback_task
 * ==================================================== */

static void playback_task_fn(void *arg)
{
    struct xiaozhi_player_s *p = (struct xiaozhi_player_s *)arg;

    uint8_t *pcm_mono = malloc(OPUS_DEC_OUT_BYTES);
    if (!pcm_mono) {
        ESP_LOGE(TAG, "playback_task OOM");
        p->task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Playback task started");

    bool     drain_pending = false;
    TickType_t drain_ts    = 0;

    while (true) {
        opus_pkt_t pkt;
        BaseType_t got = xQueueReceive(p->play_queue, &pkt, pdMS_TO_TICKS(100));

        if (got != pdTRUE) {
            /* 队列超时：仅在收到 tts_stop 后检测是否已播完 */
            if (p->tts_stop_received) {
                if (!drain_pending) {
                    drain_pending = true;
                    drain_ts = xTaskGetTickCount();
                } else if ((xTaskGetTickCount() - drain_ts) > pdMS_TO_TICKS(DRAIN_SILENCE_MS)) {
                    ESP_LOGI(TAG, "Play queue drained");
                    p->tts_stop_received = false;
                    drain_pending = false;
                    if (p->on_drained) {
                        p->on_drained(p->on_drained_ctx);
                    }
                }
            }
            continue;
        }

        /* 成功取到包，重置排空计时 */
        drain_pending = false;

        /* Opus 解码 → mono PCM */
        esp_audio_dec_in_raw_t raw = {
            .buffer        = pkt.data,
            .len           = (uint32_t)pkt.len,
            .consumed      = 0,
            .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
        };
        esp_audio_dec_out_frame_t frame = {
            .buffer       = pcm_mono,
            .len          = OPUS_DEC_OUT_BYTES,
            .needed_size  = 0,
            .decoded_size = 0,
        };
        esp_audio_dec_info_t dec_info = {0};

        esp_audio_err_t err = esp_opus_dec_decode(p->opus_decoder, &raw, &frame, &dec_info);
        if (err != ESP_AUDIO_ERR_OK || frame.decoded_size == 0) {
            if (err != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(TAG, "Opus decode err: %d", err);
            }
            continue;
        }

        /* 写入 GMF Audio Render（render 内部完成 mono→stereo 转换后调用 render_write_cb） */
        esp_audio_render_err_t rerr = esp_audio_render_stream_write(
            p->render_stream, pcm_mono, frame.decoded_size);
        if (rerr != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGW(TAG, "render_stream_write err: %d", rerr);
        }
    }

    /* 不会到达此处（任务由 xiaozhi_player_stop 强制删除） */
    free(pcm_mono);
    p->task = NULL;
    vTaskDelete(NULL);
}

/* ====================================================
 * 公开 API 实现
 * ==================================================== */

esp_err_t xiaozhi_player_init(const xiaozhi_player_config_t *config,
                               xiaozhi_player_handle_t *out_hdl)
{
    if (!config || !config->play_dev || !out_hdl) {
        return ESP_ERR_INVALID_ARG;
    }

    struct xiaozhi_player_s *p = calloc(1, sizeof(*p));
    if (!p) return ESP_ERR_NO_MEM;

    p->play_dev   = config->play_dev;
    p->sample_rate = config->sample_rate > 0 ? config->sample_rate : 16000;
    p->channels    = config->channels   > 0 ? config->channels   : 2;
    p->bits        = config->bits       > 0 ? config->bits       : 16;
    p->frame_ms    = config->frame_ms   > 0 ? config->frame_ms   : 60;

    int qsize = config->queue_size > 0 ? config->queue_size : DEFAULT_QUEUE_SIZE;

    /* Opus 解码器 */
    {
        esp_opus_dec_cfg_t dec_cfg = {
            .sample_rate    = 16000,
            .channel        = ESP_AUDIO_MONO,
            .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
            .self_delimited = false,
        };
        esp_audio_err_t err = esp_opus_dec_open(&dec_cfg, sizeof(dec_cfg), &p->opus_decoder);
        if (err != ESP_AUDIO_ERR_OK || !p->opus_decoder) {
            ESP_LOGE(TAG, "Opus decoder init failed: %d", err);
            free(p);
            return ESP_FAIL;
        }
    }

    /* GMF Pool */
    if (create_gmf_pool(&p->gmf_pool) != ESP_OK) {
        ESP_LOGE(TAG, "GMF pool creation failed");
        esp_opus_dec_close(p->opus_decoder);
        free(p);
        return ESP_FAIL;
    }

    /* GMF Audio Render */
    {
        esp_audio_render_cfg_t render_cfg = {
            .max_stream_num  = 1,
            .out_writer      = render_write_cb,
            .out_ctx         = p->play_dev,
            .out_sample_info = {
                .sample_rate     = p->sample_rate,
                .bits_per_sample = p->bits,
                .channel         = p->channels,
            },
            .pool = p->gmf_pool,
        };
        esp_audio_render_err_t rerr = esp_audio_render_create(&render_cfg, &p->render);
        if (rerr != ESP_AUDIO_RENDER_ERR_OK || !p->render) {
            ESP_LOGE(TAG, "esp_audio_render_create failed: %d", rerr);
            esp_gmf_pool_deinit(p->gmf_pool);
            esp_opus_dec_close(p->opus_decoder);
            free(p);
            return ESP_FAIL;
        }
        esp_audio_render_stream_get(p->render, ESP_AUDIO_RENDER_FIRST_STREAM,
                                    &p->render_stream);
    }

    /* 播放队列 */
    p->play_queue = xQueueCreate(qsize, sizeof(opus_pkt_t));
    if (!p->play_queue) {
        ESP_LOGE(TAG, "play_queue create failed");
        esp_audio_render_destroy(p->render);
        esp_gmf_pool_deinit(p->gmf_pool);
        esp_opus_dec_close(p->opus_decoder);
        free(p);
        return ESP_ERR_NO_MEM;
    }

    *out_hdl = p;
    ESP_LOGI(TAG, "Player init OK");
    return ESP_OK;
}

esp_err_t xiaozhi_player_deinit(xiaozhi_player_handle_t hdl)
{
    if (!hdl) return ESP_ERR_INVALID_ARG;
    struct xiaozhi_player_s *p = hdl;

    /* 调用方须先 stop */
    esp_audio_render_destroy(p->render);
    esp_gmf_pool_deinit(p->gmf_pool);
    esp_opus_dec_close(p->opus_decoder);
    vQueueDelete(p->play_queue);
    free(p);
    ESP_LOGI(TAG, "Player deinit OK");
    return ESP_OK;
}

esp_err_t xiaozhi_player_start(xiaozhi_player_handle_t hdl)
{
    if (!hdl) return ESP_ERR_INVALID_ARG;
    struct xiaozhi_player_s *p = hdl;

    /* 打开 Audio Render 流（输入：mono 16kHz 16-bit；render 自动转换为 stereo 输出） */
    esp_audio_render_sample_info_t stream_info = {
        .sample_rate     = 16000,
        .bits_per_sample = p->bits,
        .channel         = 1,  /* mono 输入 */
    };
    esp_audio_render_err_t rerr = esp_audio_render_stream_open(p->render_stream, &stream_info);
    if (rerr != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "render_stream_open failed: %d", rerr);
        return ESP_FAIL;
    }

    p->tts_stop_received = false;
    xQueueReset(p->play_queue);

    xTaskCreate(playback_task_fn, "xz_play", PLAYBACK_TASK_STACK, p,
                PLAYBACK_TASK_PRIO, &p->task);
    if (!p->task) {
        ESP_LOGE(TAG, "playback task create failed");
        esp_audio_render_stream_close(p->render_stream);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Player started");
    return ESP_OK;
}

esp_err_t xiaozhi_player_stop(xiaozhi_player_handle_t hdl)
{
    if (!hdl) return ESP_ERR_INVALID_ARG;
    struct xiaozhi_player_s *p = hdl;

    /* 等待任务自然退出（最多 2s）；playback_task 目前是无限循环，直接强制删除 */
    if (p->task) {
        vTaskDelete(p->task);
        p->task = NULL;
    }

    if (p->render_stream) {
        esp_audio_render_stream_close(p->render_stream);
    }

    xQueueReset(p->play_queue);
    p->tts_stop_received = false;

    ESP_LOGI(TAG, "Player stopped");
    return ESP_OK;
}

void xiaozhi_player_push(xiaozhi_player_handle_t hdl,
                          const uint8_t *data, int len)
{
    if (!hdl || !data || len <= 0) return;
    struct xiaozhi_player_s *p = hdl;

    opus_pkt_t pkt;
    int copy_len = (len < (int)sizeof(pkt.data)) ? len : (int)sizeof(pkt.data);
    memcpy(pkt.data, data, copy_len);
    pkt.len = copy_len;

    if (xQueueSend(p->play_queue, &pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Play queue full, drop audio frame");
    }
}

void xiaozhi_player_flush(xiaozhi_player_handle_t hdl)
{
    if (!hdl) return;
    struct xiaozhi_player_s *p = hdl;
    xQueueReset(p->play_queue);
    p->tts_stop_received = false;
}

void xiaozhi_player_notify_tts_stop(xiaozhi_player_handle_t hdl)
{
    if (!hdl) return;
    struct xiaozhi_player_s *p = hdl;
    p->tts_stop_received = true;
}

void xiaozhi_player_set_on_drained(xiaozhi_player_handle_t hdl,
                                    xiaozhi_player_drained_cb_t cb,
                                    void *cb_ctx)
{
    if (!hdl) return;
    struct xiaozhi_player_s *p = hdl;
    p->on_drained     = cb;
    p->on_drained_ctx = cb_ctx;
}
