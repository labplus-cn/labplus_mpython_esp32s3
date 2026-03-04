/*
 * xiaozhi_session.c - 小智AI语音会话核心实现
 *
 * 实现了与小智AI服务器的 WebSocket 语音会话功能：
 *   - 连接/断开 WebSocket
 *   - Hello 握手协议
 *   - 麦克风音频 → Opus → WebSocket 发送
 *   - WebSocket 接收 → Opus 解码 → 扬声器播放
 *   - JSON 消息处理 (STT/TTS/LLM 事件)
 *   - 状态机管理
 *
 * 音频约定:
 *   - 硬件采样率: 16000 Hz, 2 声道 (stereo), 16-bit PCM
 *     （需先调用 audio.init() 或手动初始化音频硬件）
 *   - Opus: 16000 Hz, mono, 60ms 帧, 32kbps VBR, VOIP 模式
 *   - 收音 (GMF pipeline):
 *       esp_capture sink 请求 Opus 格式（mono 16kHz），
 *       GMF 内部 pipeline: [aud_ch_cvt] stereo→mono → [aud_enc] Opus 编码，
 *       capture_task 直接获取 Opus 帧并入队发送，无需手动编码
 *   - 播放:
 *       Opus 解码 → mono PCM → esp_audio_render（GMF 内部 mono→stereo）→ 硬件
 *
 * 前置条件:
 *   调用 xiaozhi_init() 之前，必须先完成音频硬件初始化
 *   （在 MicroPython 层调用 audio.init() 或在 C 层调用
 *    esp_gmf_app_setup_codec_dev()）。
 */

#include "xiaozhi_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_websocket_client.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

/* Opus 解码器（播放侧） */
#include "esp_audio_types.h"
#include "esp_audio_dec.h"
#include "esp_opus_dec.h"

/* GMF 音频硬件 */
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_codec_dev.h"

/* GMF Pool + 音频元素（用于 esp_audio_render 格式转换） */
#include "esp_gmf_pool.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"

/* GMF Audio Render（播放） */
#include "esp_audio_render.h"
#include "esp_audio_render_types.h"

/* 录音 pipeline 封装 */
#include "audio_capture.h"

/* ====================================================
 * 常量和宏定义
 * ==================================================== */

#define TAG "xiaozhi"

/* 音频硬件参数（与 audio.init() 中的参数保持一致） */
#define AUDIO_SAMPLE_RATE    16000
#define AUDIO_CHANNELS       2           /* 硬件为 stereo */
#define AUDIO_BITS           16
#define OPUS_FRAME_MS        60
#define OPUS_SAMPLE_RATE     16000
#define OPUS_CHANNELS        1           /* Opus mono */

/* Opus 解码输出最大大小 (60ms @ 16kHz mono 16-bit, 留余量) */
#define OPUS_DEC_OUT_BYTES   4096

/* 播放队列中单帧最大字节数 */
#define OPUS_OUT_MAX_BYTES   512

/* 队列深度 */
#define PLAY_QUEUE_SIZE      20

/* FreeRTOS 事件位 */
#define EVT_CONNECTED        (1 << 0)  /* 收到服务器 hello，握手完成 */
#define EVT_DISCONNECTED     (1 << 1)  /* WebSocket 断开 */
#define EVT_STOP_REQUESTED   (1 << 2)  /* 上层请求停止 */

/* WebSocket 接收缓冲区 */
#define WS_RECV_BUF_SIZE     8192

/* FreeRTOS 任务参数 */
#define CAPTURE_TASK_STACK   6144
#define CAPTURE_TASK_PRIO    7
#define PLAYBACK_TASK_STACK  8192
#define PLAYBACK_TASK_PRIO   7

/* ====================================================
 * 内部数据结构
 * ==================================================== */

/** Opus 数据包（用于 play_queue：WS 接收 → 播放任务） */
typedef struct {
    uint8_t data[OPUS_OUT_MAX_BYTES];
    int     len;
} opus_packet_t;

/** 全局会话状态 */
typedef struct {
    /* 配置 */
    char url[256];
    char token[256];
    char device_id[32];
    char client_id[64];
    int  frame_ms;

    /* 状态 */
    volatile xiaozhi_state_t state;
    volatile bool recording_active;  /* true=正在上传音频；由 listen_start/stop 控制 */
    char session_id[64];
    bool hello_received;

    /* WebSocket */
    esp_websocket_client_handle_t ws_client;

    /* Opus 解码器句柄（播放侧：Opus→PCM） */
    void *opus_decoder;

    /* 音频播放设备句柄 */
    esp_codec_dev_handle_t play_dev;

    /* 录音设备句柄（由 xiaozhi_start() 用于 record_pipe_open） */
    esp_codec_dev_handle_t rec_dev;

    /* GMF Pool（供 esp_audio_render 格式转换使用） */
    esp_gmf_pool_handle_t  gmf_pool;

    /* GMF Audio Render（播放：Opus 解码 PCM → 扬声器） */
    esp_audio_render_handle_t        render;
    esp_audio_render_stream_handle_t render_stream;

    /* 录音 pipeline */
    record_pipe_handle_t rec_pipe;

    /* 队列 */
    QueueHandle_t play_queue;  /* ws_recv → playback */

    /* FreeRTOS 同步 */
    EventGroupHandle_t event_group;
    SemaphoreHandle_t  state_mutex;

    /* 任务句柄 */
    TaskHandle_t capture_task;
    TaskHandle_t playback_task;

    /* 用户回调 */
    xiaozhi_on_state_t     on_state;
    xiaozhi_on_stt_t       on_stt;
    xiaozhi_on_tts_state_t on_tts_state;
    xiaozhi_on_llm_t       on_llm;
    xiaozhi_on_message_t   on_message;
    xiaozhi_on_wakeup_t    on_wakeup;

    /* 唤醒词模式：true=连接后进入 CONNECTED，false=直接进入 LISTENING */
    /* (已移除，统一使用 LISTENING 空闲状态) */

} xiaozhi_ctx_t;

static xiaozhi_ctx_t *s_ctx = NULL;

/* ====================================================
 * 内部函数声明
 * ==================================================== */

static void set_state(xiaozhi_state_t new_state);
static esp_err_t ws_send_text(const char *text);
static esp_err_t send_hello(void);
static esp_err_t send_listen_start(void);
static esp_err_t send_listen_stop(void);
static esp_err_t send_abort_msg(void);
static void handle_json_message(const char *json_str);
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data);
static void capture_task_fn(void *arg);
static void playback_task_fn(void *arg);
static void get_mac_str(char *buf, size_t len);

/* ====================================================
 * GMF 辅助函数
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
 * 状态管理
 * ==================================================== */

static void set_state(xiaozhi_state_t new_state)
{
    if (!s_ctx) return;
    xSemaphoreTake(s_ctx->state_mutex, portMAX_DELAY);
    xiaozhi_state_t old = s_ctx->state;
    s_ctx->state = new_state;
    xSemaphoreGive(s_ctx->state_mutex);

    if (old != new_state) {
        ESP_LOGI(TAG, "State: %d -> %d", (int)old, (int)new_state);
        if (s_ctx->on_state) {
            s_ctx->on_state(new_state);
        }
    }
}

/* ====================================================
 * JSON 消息发送
 * ==================================================== */

static esp_err_t ws_send_text(const char *text)
{
    if (!s_ctx->ws_client || !esp_websocket_client_is_connected(s_ctx->ws_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_websocket_client_send_text(s_ctx->ws_client, text,
                                             (int)strlen(text),
                                             pdMS_TO_TICKS(2000));
    if (ret < 0) {
        ESP_LOGE(TAG, "ws_send_text failed: %d", ret);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "ws_send_text ok, bytes=%d", ret);
    return ESP_OK;
}

static esp_err_t send_hello(void)
{
    cJSON *root  = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON *features = cJSON_AddObjectToObject(root, "features");
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddStringToObject(root, "transport", "websocket");

    cJSON *audio = cJSON_AddObjectToObject(root, "audio_params");
    cJSON_AddStringToObject(audio, "format", "opus");
    cJSON_AddNumberToObject(audio, "sample_rate", OPUS_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio, "channels", OPUS_CHANNELS);
    cJSON_AddNumberToObject(audio, "frame_duration", s_ctx->frame_ms);

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "→ hello: %s", str);
    esp_err_t err = ws_send_text(str);
    free(str);
    return err;
}

static esp_err_t send_listen_start(void)
{
    cJSON *root = cJSON_CreateObject();
    if (s_ctx->session_id[0]) {
        cJSON_AddStringToObject(root, "session_id", s_ctx->session_id);
    }
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", "auto");

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    ESP_LOGD(TAG, "→ listen start");
    esp_err_t err = ws_send_text(str);
    free(str);
    return err;
}

static esp_err_t send_listen_stop(void)
{
    cJSON *root = cJSON_CreateObject();
    if (s_ctx->session_id[0]) {
        cJSON_AddStringToObject(root, "session_id", s_ctx->session_id);
    }
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t err = ws_send_text(str);
    free(str);
    return err;
}

static esp_err_t send_abort_msg(void)
{
    cJSON *root = cJSON_CreateObject();
    if (s_ctx->session_id[0]) {
        cJSON_AddStringToObject(root, "session_id", s_ctx->session_id);
    }
    cJSON_AddStringToObject(root, "type", "abort");
    cJSON_AddStringToObject(root, "reason", "user");

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t err = ws_send_text(str);
    free(str);
    return err;
}

/* ====================================================
 * JSON 消息接收处理
 * ==================================================== */

static void handle_json_message(const char *json_str)
{
    /* 触发原始消息回调 */
    if (s_ctx->on_message) {
        s_ctx->on_message(json_str);
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse error: %.80s", json_str);
        return;
    }

    cJSON *type_j = cJSON_GetObjectItem(root, "type");
    if (!type_j || !cJSON_IsString(type_j)) {
        cJSON_Delete(root);
        return;
    }
    const char *type = type_j->valuestring;

    /* ---- hello ---- */
    if (strcmp(type, "hello") == 0) {
        cJSON *transport_j = cJSON_GetObjectItem(root, "transport");
        if (transport_j && cJSON_IsString(transport_j) &&
            strcmp(transport_j->valuestring, "websocket") == 0) {
            cJSON *sid_j = cJSON_GetObjectItem(root, "session_id");
            if (sid_j && cJSON_IsString(sid_j)) {
                strncpy(s_ctx->session_id, sid_j->valuestring,
                        sizeof(s_ctx->session_id) - 1);
            }
            s_ctx->hello_received = true;
            ESP_LOGI(TAG, "← hello ok, session=%s", s_ctx->session_id);
            set_state(XIAOZHI_STATE_LISTENING);  /* 握手完成，进入空闲待机 */
            xEventGroupSetBits(s_ctx->event_group, EVT_CONNECTED);
        }
    }
    /* ---- stt ---- */
    else if (strcmp(type, "stt") == 0) {
        cJSON *text_j = cJSON_GetObjectItem(root, "text");
        if (text_j && cJSON_IsString(text_j)) {
            ESP_LOGI(TAG, "← stt: %s", text_j->valuestring);
            if (s_ctx->on_stt) s_ctx->on_stt(text_j->valuestring);
        }
    }
    /* ---- tts ---- */
    else if (strcmp(type, "tts") == 0) {
        cJSON *state_j = cJSON_GetObjectItem(root, "state");
        cJSON *text_j  = cJSON_GetObjectItem(root, "text");
        const char *tts_state = (state_j && cJSON_IsString(state_j))
                                ? state_j->valuestring : "";
        const char *tts_text  = (text_j  && cJSON_IsString(text_j))
                                ? text_j->valuestring  : NULL;

        ESP_LOGI(TAG, "← tts state=%s", tts_state);

        if (strcmp(tts_state, "start") == 0) {
            /* 服务器开始 TTS：停止录音上传，清空播放队列，切换为 SPEAKING */
            s_ctx->recording_active = false;
            opus_packet_t tmp;
            while (xQueueReceive(s_ctx->play_queue, &tmp, 0) == pdTRUE) {}
            set_state(XIAOZHI_STATE_SPEAKING);
        } else if (strcmp(tts_state, "stop") == 0) {
            /* TTS 结束后等待播放队列清空，再切回 LISTENING */
            /* 由 playback_task 检测队列空并自动切换 */
        }

        if (s_ctx->on_tts_state) s_ctx->on_tts_state(tts_state, tts_text);
    }
    /* ---- llm ---- */
    else if (strcmp(type, "llm") == 0) {
        cJSON *emotion_j = cJSON_GetObjectItem(root, "emotion");
        cJSON *text_j    = cJSON_GetObjectItem(root, "text");
        const char *em   = (emotion_j && cJSON_IsString(emotion_j))
                           ? emotion_j->valuestring : "";
        const char *txt  = (text_j    && cJSON_IsString(text_j))
                           ? text_j->valuestring    : "";
        if (s_ctx->on_llm) s_ctx->on_llm(em, txt);
    }
    /* ---- system ---- */
    else if (strcmp(type, "system") == 0) {
        cJSON *cmd_j = cJSON_GetObjectItem(root, "command");
        if (cmd_j && cJSON_IsString(cmd_j) &&
            strcmp(cmd_j->valuestring, "reboot") == 0) {
            ESP_LOGW(TAG, "Server requested reboot");
            esp_restart();
        }
    }

    cJSON_Delete(root);
}

/* ====================================================
 * WebSocket 事件处理
 * ==================================================== */

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    if (!s_ctx) return;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected, sending hello");
        set_state(XIAOZHI_STATE_CONNECTED);
        if (send_hello() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send hello");
            xEventGroupSetBits(s_ctx->event_group, EVT_DISCONNECTED);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WS disconnected");
        xEventGroupSetBits(s_ctx->event_group, EVT_DISCONNECTED);
        set_state(XIAOZHI_STATE_IDLE);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!data) break;
        /* 关闭帧：记录状态码和原因（辅助调试） */
        if (data->op_code == WS_TRANSPORT_OPCODES_CLOSE) {  /* opcode 8 = close */
            if (data->data_len >= 2) {
                uint16_t code = ((uint8_t)data->data_ptr[0] << 8) | (uint8_t)data->data_ptr[1];
                ESP_LOGW(TAG, "← WS close frame: code=%u, reason=%.*s",
                         code, data->data_len - 2, data->data_ptr + 2);
            } else {
                ESP_LOGW(TAG, "← WS close frame: no code");
            }
            break;
        }
        if (!data->data_ptr || data->data_len <= 0) break;
        if (data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
            /* 二进制：Opus 音频帧（仅 SPEAKING 状态下入队） */
            ESP_LOGI(TAG, "← binary %d bytes (Opus)", data->data_len);

            if (s_ctx->state == XIAOZHI_STATE_SPEAKING) {
                opus_packet_t pkt;
                int copy_len = (data->data_len < (int)sizeof(pkt.data))
                               ? data->data_len : (int)sizeof(pkt.data);
                memcpy(pkt.data, data->data_ptr, copy_len);
                pkt.len = copy_len;
                if (xQueueSend(s_ctx->play_queue, &pkt, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Play queue full, drop audio frame");
                }
            }
        } else if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
            /* 文本：JSON 控制消息 */
            /* 注意：data->data_ptr 可能不以 null 结尾 */
            ESP_LOGI(TAG, "← text %d bytes", data->data_len);
            char *buf = malloc(data->data_len + 1);
            if (buf) {
                memcpy(buf, data->data_ptr, data->data_len);
                buf[data->data_len] = '\0';
                handle_json_message(buf);
                free(buf);
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        xEventGroupSetBits(s_ctx->event_group, EVT_DISCONNECTED);
        set_state(XIAOZHI_STATE_ERROR);
        break;

    default:
        break;
    }
}

/* 前向声明：session_wakeup_cb 定义在下方，capture_task_fn 中需要用到 */
static void session_wakeup_cb(void *ctx);

/* ====================================================
 * 音频捕获发送任务：GMF pipeline Opus 帧 → WebSocket
 *
 * GMF capture pipeline 已完成 stereo→mono + Opus 编码，
 * 此处直接获取 Opus 帧并通过 WebSocket 发送，无需中间队列。
 * ==================================================== */

static void capture_task_fn(void *arg)
{
    /* 在任务内部打开并启动录音 pipeline：任务本身即消费者，AFE 启动时立即有下游排水 */
    if (record_pipe_open(s_ctx->rec_dev, session_wakeup_cb, NULL, &s_ctx->rec_pipe) != ESP_OK) {
        ESP_LOGE(TAG, "capture_task: record_pipe_open failed");
        set_state(XIAOZHI_STATE_ERROR);
        s_ctx->capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (record_pipe_start(s_ctx->rec_pipe) != ESP_OK) {
        ESP_LOGE(TAG, "capture_task: record_pipe_start failed");
        record_pipe_close(s_ctx->rec_pipe);
        s_ctx->rec_pipe = NULL;
        set_state(XIAOZHI_STATE_ERROR);
        s_ctx->capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Capture task started");

    while (true) {
        if (xEventGroupGetBits(s_ctx->event_group) & EVT_STOP_REQUESTED) break;

        const uint8_t *data = NULL;
        int size = 0;
        record_pipe_acquire_frame(s_ctx->rec_pipe, &data, &size);

        if (size <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* recording_active=true 且非 SPEAKING 时，通过 WebSocket 发送 Opus 帧 */
        if (s_ctx->recording_active &&
            s_ctx->state != XIAOZHI_STATE_SPEAKING &&
            s_ctx->ws_client &&
            esp_websocket_client_is_connected(s_ctx->ws_client)) {
            int ret = esp_websocket_client_send_bin(s_ctx->ws_client,
                                                    (const char *)data,
                                                    size,
                                                    pdMS_TO_TICKS(0));
            if (ret < 0) {
                ESP_LOGW(TAG, "WS send_bin failed (drop frame): %d", ret);
            }
        }
        /* recording_active=false 时丢弃帧（防止 pipeline 堵死） */

        record_pipe_release_frame(s_ctx->rec_pipe);
    }

    /* 正常退出：停止并关闭录音 pipeline；xiaozhi_stop 中的 NULL 检查会跳过重复操作 */
    if (s_ctx->rec_pipe) {
        record_pipe_stop(s_ctx->rec_pipe);
        record_pipe_close(s_ctx->rec_pipe);
        s_ctx->rec_pipe = NULL;
    }

    ESP_LOGI(TAG, "Capture task done");
    s_ctx->capture_task = NULL;
    vTaskDelete(NULL);
}

/* ====================================================
 * 音频播放任务：播放队列 → Opus 解码 → 扬声器
 * ==================================================== */

static void playback_task_fn(void *arg)
{
    /* render 负责 mono→stereo 转换，只需 mono 缓冲区 */
    uint8_t *pcm_mono = malloc(OPUS_DEC_OUT_BYTES);

    if (!pcm_mono) {
        ESP_LOGE(TAG, "playback_task OOM");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Playback task started");

    bool tts_stop_pending = false;
    TickType_t tts_stop_ts = 0;

    while (true) {
        if (xEventGroupGetBits(s_ctx->event_group) & EVT_STOP_REQUESTED) break;

        opus_packet_t pkt;
        BaseType_t got = xQueueReceive(s_ctx->play_queue, &pkt, pdMS_TO_TICKS(100));
        
        if (got != pdTRUE) {
            if (s_ctx->state == XIAOZHI_STATE_SPEAKING) {
                ESP_LOGI(TAG, "Playback: got opus packet..., len=%d", pkt.len);
                if (!tts_stop_pending) {
                    tts_stop_pending = true;
                    tts_stop_ts = xTaskGetTickCount();
                } else if ((xTaskGetTickCount() - tts_stop_ts) > pdMS_TO_TICKS(200)) {
                    ESP_LOGI(TAG, "Play queue drained, back to LISTENING");
                    s_ctx->recording_active = false;
                    set_state(XIAOZHI_STATE_LISTENING);
                    tts_stop_pending = false;
                }
            }
            continue;
        }
        tts_stop_pending = false;

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

        esp_audio_err_t err = esp_opus_dec_decode(s_ctx->opus_decoder,
                                                  &raw, &frame, &dec_info);
        if (err != ESP_AUDIO_ERR_OK || frame.decoded_size == 0) {
            if (err != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(TAG, "Opus decode err: %d", err);
            }
            continue;
        }

        /* 写入 GMF Audio Render（render 内部完成 mono→stereo 转换后调用 render_write_cb） */
        esp_audio_render_err_t rerr = esp_audio_render_stream_write(
            s_ctx->render_stream, pcm_mono, frame.decoded_size);
        if (rerr != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGW(TAG, "render_stream_write err: %d", rerr);
        }
    }

cleanup:
    ESP_LOGI(TAG, "Playback task done");
    free(pcm_mono);
    s_ctx->playback_task = NULL;
    vTaskDelete(NULL);
}

/* ====================================================
 * MAC 地址字符串
 * ==================================================== */

static void get_mac_str(char *buf, size_t len)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ====================================================
 * 公开 API
 * ==================================================== */

/* AFE 唤醒词检测回调：从 record_pipe 的 AFE 任务中调用 */
static void session_wakeup_cb(void *ctx)
{
    (void)ctx;
    xiaozhi_trigger_wakeup();
}

esp_err_t xiaozhi_init(const xiaozhi_config_t *config)
{
    if (!config || !config->url || !config->token) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ctx) {
        ESP_LOGW(TAG, "Already init, call deinit first");
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx = calloc(1, sizeof(xiaozhi_ctx_t));
    if (!s_ctx) return ESP_ERR_NO_MEM;

    /* 复制配置 */
    strncpy(s_ctx->url,   config->url,   sizeof(s_ctx->url)   - 1);
    strncpy(s_ctx->token, config->token, sizeof(s_ctx->token) - 1);
    s_ctx->frame_ms         = (config->frame_duration_ms > 0) ? config->frame_duration_ms : 60;

    /* 设备 ID */
    if (config->device_id && config->device_id[0]) {
        strncpy(s_ctx->device_id, config->device_id, sizeof(s_ctx->device_id) - 1);
    } else {
        get_mac_str(s_ctx->device_id, sizeof(s_ctx->device_id));
    }

    /* 客户端 UUID：从 NVS board/uuid 读取（与 activate.c 保持一致） */
    if (config->client_id && config->client_id[0]) {
        strncpy(s_ctx->client_id, config->client_id, sizeof(s_ctx->client_id) - 1);
    } else {
        nvs_handle_t nvs_h;
        bool got_uuid = false;
        if (nvs_open("board", NVS_READONLY, &nvs_h) == ESP_OK) {
            size_t uuid_len = sizeof(s_ctx->client_id);
            if (nvs_get_str(nvs_h, "uuid", s_ctx->client_id, &uuid_len) == ESP_OK
                    && s_ctx->client_id[0] != '\0') {
                got_uuid = true;
            }
            nvs_close(nvs_h);
        }
        if (!got_uuid) {
            /* NVS 中无 UUID（未激活），降级为 MAC 派生 */
            uint8_t mac[6] = {0};
            esp_wifi_get_mac(WIFI_IF_STA, mac);
            snprintf(s_ctx->client_id, sizeof(s_ctx->client_id),
                     "%02x%02x%02x%02x-%02x%02x-4%02x%02x-8%02x%02x-%02x%02x%02x%02x%02x%02x",
                     mac[0], mac[1], mac[2], mac[3],
                     mac[4], mac[5],
                     mac[0] ^ 0x5A, mac[1],
                     mac[2] | 0x80, mac[3],
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }

    ESP_LOGI(TAG, "Config: url=%s, token=%s, device_id=%s, client_id=%s, frame_ms=%d",
             s_ctx->url, s_ctx->token, s_ctx->device_id, s_ctx->client_id, s_ctx->frame_ms);

    /* 获取音频硬件句柄（必须先调用 audio.init() 或 esp_gmf_app_setup_codec_dev()） */
    esp_codec_dev_handle_t rec_dev  = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    s_ctx->play_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();

    if (!rec_dev || !s_ctx->play_dev) {
        ESP_LOGE(TAG, "Codec device not ready. Call audio.init() first.");
        free(s_ctx);
        s_ctx = NULL;
        return ESP_ERR_INVALID_STATE;
    }
    s_ctx->rec_dev = rec_dev;

    /* 设置音量和增益（可选） */
    if (config->output_volume > 0) {
        esp_codec_dev_set_out_vol(s_ctx->play_dev, config->output_volume);
    }
    if (config->input_gain_db > 0.0f) {
        esp_codec_dev_set_in_gain(rec_dev, config->input_gain_db);
    }

    /* Opus 解码器（播放侧：Opus→PCM，由 playback_task 使用） */
    {
        esp_opus_dec_cfg_t dec_cfg = {
            .sample_rate    = OPUS_SAMPLE_RATE,
            .channel        = ESP_AUDIO_MONO,
            .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
            .self_delimited = false,
        };
        esp_audio_err_t err = esp_opus_dec_open(&dec_cfg, sizeof(dec_cfg),
                                                &s_ctx->opus_decoder);
        if (err != ESP_AUDIO_ERR_OK || !s_ctx->opus_decoder) {
            ESP_LOGE(TAG, "Opus decoder init failed: %d", err);
            free(s_ctx); s_ctx = NULL;
            return ESP_FAIL;
        }
    }

    /* GMF Pool（供 esp_audio_render 格式转换使用） */
    if (create_gmf_pool(&s_ctx->gmf_pool) != ESP_OK) {
        ESP_LOGE(TAG, "GMF pool creation failed");
        esp_opus_dec_close(s_ctx->opus_decoder);
        free(s_ctx); s_ctx = NULL;
        return ESP_FAIL;
    }

    /* GMF Audio Render（播放） */
    {
        esp_audio_render_cfg_t render_cfg = {
            .max_stream_num  = 1,
            .out_writer      = render_write_cb,
            .out_ctx         = s_ctx->play_dev,
            .out_sample_info = {
                .sample_rate     = AUDIO_SAMPLE_RATE,   /* 16000 Hz */
                .bits_per_sample = AUDIO_BITS,          /* 16-bit */
                .channel         = AUDIO_CHANNELS,      /* stereo */
            },
            .pool = s_ctx->gmf_pool,
        };
        esp_audio_render_err_t rerr = esp_audio_render_create(&render_cfg, &s_ctx->render);
        if (rerr != ESP_AUDIO_RENDER_ERR_OK || !s_ctx->render) {
            ESP_LOGE(TAG, "esp_audio_render_create failed: %d", rerr);
            esp_gmf_pool_deinit(s_ctx->gmf_pool);
            esp_opus_dec_close(s_ctx->opus_decoder);
            free(s_ctx); s_ctx = NULL;
            return ESP_FAIL;
        }
        /* 获取第一个（也是唯一的）流句柄 */
        esp_audio_render_stream_get(s_ctx->render,
                                    ESP_AUDIO_RENDER_FIRST_STREAM,
                                    &s_ctx->render_stream);
    }

    /* FreeRTOS 资源 */
    s_ctx->play_queue  = xQueueCreate(PLAY_QUEUE_SIZE,  sizeof(opus_packet_t));
    s_ctx->event_group = xEventGroupCreate();
    s_ctx->state_mutex = xSemaphoreCreateMutex();

    if (!s_ctx->play_queue || !s_ctx->event_group || !s_ctx->state_mutex) {
        ESP_LOGE(TAG, "FreeRTOS object creation failed");
        if (s_ctx->play_queue)  vQueueDelete(s_ctx->play_queue);
        if (s_ctx->event_group) vEventGroupDelete(s_ctx->event_group);
        if (s_ctx->state_mutex) vSemaphoreDelete(s_ctx->state_mutex);
        esp_audio_render_destroy(s_ctx->render);
        esp_gmf_pool_deinit(s_ctx->gmf_pool);
        esp_opus_dec_close(s_ctx->opus_decoder);
        free(s_ctx); s_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx->state = XIAOZHI_STATE_IDLE;
    ESP_LOGI(TAG, "Init OK");
    return ESP_OK;
}

esp_err_t xiaozhi_deinit(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;

    xiaozhi_stop();  /* 确保任务和连接已停止 */

    esp_opus_dec_close(s_ctx->opus_decoder);

    /* 销毁录音 pipeline */
    if (s_ctx->rec_pipe) {
        record_pipe_close(s_ctx->rec_pipe);
        s_ctx->rec_pipe = NULL;
    }

    /* 销毁 GMF Audio Render */
    if (s_ctx->render) {
        esp_audio_render_destroy(s_ctx->render);
        s_ctx->render = NULL;
    }

    /* 销毁 GMF Pool */
    if (s_ctx->gmf_pool) {
        esp_gmf_pool_deinit(s_ctx->gmf_pool);
        s_ctx->gmf_pool = NULL;
    }

    vQueueDelete(s_ctx->play_queue);
    vEventGroupDelete(s_ctx->event_group);
    vSemaphoreDelete(s_ctx->state_mutex);

    free(s_ctx);
    s_ctx = NULL;
    ESP_LOGI(TAG, "Deinit OK");
    return ESP_OK;
}

esp_err_t xiaozhi_start(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;
    if (s_ctx->state != XIAOZHI_STATE_IDLE &&
        s_ctx->state != XIAOZHI_STATE_ERROR) {
        ESP_LOGW(TAG, "Not idle, current state: %d", (int)s_ctx->state);
        return ESP_ERR_INVALID_STATE;
    }

    set_state(XIAOZHI_STATE_CONNECTING);
    xEventGroupClearBits(s_ctx->event_group, 0xFF);
    s_ctx->hello_received = false;
    memset(s_ctx->session_id, 0, sizeof(s_ctx->session_id));
    xQueueReset(s_ctx->play_queue);

    /* 构造 Authorization 头
     * 参考实现：若 token 中已含空格（如 "Bearer xxxx"），则不再加前缀 */
    char auth_hdr[300];
    if (strchr(s_ctx->token, ' ') == NULL) {
        snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", s_ctx->token);
    } else {
        strncpy(auth_hdr, s_ctx->token, sizeof(auth_hdr) - 1);
        auth_hdr[sizeof(auth_hdr) - 1] = '\0';
    }
    ESP_LOGI(TAG, "auth_hdr: %s", auth_hdr);
    /* WebSocket 配置 */
    esp_websocket_client_config_t ws_cfg = {
        .uri                    = s_ctx->url,
        .buffer_size            = WS_RECV_BUF_SIZE,
        .task_stack             = 8192,
        .task_prio              = 5,
        .reconnect_timeout_ms   = 10000,
        .network_timeout_ms     = 10000,
        .disable_auto_reconnect = true,
    };

    s_ctx->ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ctx->ws_client) {
        ESP_LOGE(TAG, "WS client init failed");
        set_state(XIAOZHI_STATE_ERROR);
        return ESP_FAIL;
    }

    /* 添加 HTTP 请求头 */
    esp_websocket_client_append_header(s_ctx->ws_client, "Authorization",    auth_hdr);
    esp_websocket_client_append_header(s_ctx->ws_client, "Protocol-Version", "1");
    esp_websocket_client_append_header(s_ctx->ws_client, "Device-Id",        s_ctx->device_id);
    esp_websocket_client_append_header(s_ctx->ws_client, "Client-Id",        s_ctx->client_id);

    /* 注册事件回调 */
    esp_websocket_register_events(s_ctx->ws_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);

    /* 启动连接 */
    esp_err_t err = esp_websocket_client_start(s_ctx->ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WS start failed: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(s_ctx->ws_client);
        s_ctx->ws_client = NULL;
        set_state(XIAOZHI_STATE_ERROR);
        return err;
    }

    /* 等待服务器 hello 响应 (10s)；hello 已在 WEBSOCKET_EVENT_CONNECTED 回调中发送 */
    EventBits_t bits = xEventGroupWaitBits(s_ctx->event_group,
                                           EVT_CONNECTED | EVT_DISCONNECTED,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (!(bits & EVT_CONNECTED) || (bits & EVT_DISCONNECTED)) {
        ESP_LOGE(TAG, "Server hello timeout or disconnected");
        esp_websocket_client_stop(s_ctx->ws_client);
        esp_websocket_client_destroy(s_ctx->ws_client);
        s_ctx->ws_client = NULL;
        set_state(XIAOZHI_STATE_ERROR);
        return ESP_ERR_TIMEOUT;
    }

    /* 打开 Audio Render 流（输入：mono 16kHz 16-bit；render 自动转换为 stereo 输出） */
    {
        esp_audio_render_sample_info_t stream_info = {
            .sample_rate     = OPUS_SAMPLE_RATE,  /* 16000 Hz */
            .bits_per_sample = AUDIO_BITS,        /* 16-bit */
            .channel         = OPUS_CHANNELS,     /* mono */
        };
        esp_audio_render_err_t rerr = esp_audio_render_stream_open(
            s_ctx->render_stream, &stream_info);
        if (rerr != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "render_stream_open failed: %d", rerr);
            esp_websocket_client_stop(s_ctx->ws_client);
            esp_websocket_client_destroy(s_ctx->ws_client);
            s_ctx->ws_client = NULL;
            set_state(XIAOZHI_STATE_ERROR);
            return ESP_FAIL;
        }
    }

    /* 启动音频任务（capture 任务内部负责 record_pipe open+start） */
    xTaskCreate(capture_task_fn,  "xz_cap",  CAPTURE_TASK_STACK,  NULL,
                CAPTURE_TASK_PRIO,  &s_ctx->capture_task);
    xTaskCreate(playback_task_fn, "xz_play", PLAYBACK_TASK_STACK, NULL,
                PLAYBACK_TASK_PRIO, &s_ctx->playback_task);

    /* 握手完成，已进入 LISTENING 空闲状态，等待唤醒或 listen_start() 触发 */
    ESP_LOGI(TAG, "Session started, waiting for trigger");
    return ESP_OK;
}

esp_err_t xiaozhi_stop(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;
    if (s_ctx->state == XIAOZHI_STATE_IDLE) return ESP_OK;

    ESP_LOGI(TAG, "Stopping");

    /* 发停止信号给任务 */
    xEventGroupSetBits(s_ctx->event_group, EVT_STOP_REQUESTED);

    /* 发送 goodbye */
    if (s_ctx->ws_client && esp_websocket_client_is_connected(s_ctx->ws_client)) {
        send_listen_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* 等待任务退出（最多 2s）*/
    int timeout_ms = 2000;
    while ((s_ctx->capture_task || s_ctx->playback_task) && timeout_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        timeout_ms -= 50;
    }
    /* 超时强制删除 */
    if (s_ctx->capture_task)  { vTaskDelete(s_ctx->capture_task);  s_ctx->capture_task  = NULL; }
    if (s_ctx->playback_task) { vTaskDelete(s_ctx->playback_task); s_ctx->playback_task = NULL; }

    /* 停止 WebSocket */
    if (s_ctx->ws_client) {
        esp_websocket_client_stop(s_ctx->ws_client);
        esp_websocket_client_destroy(s_ctx->ws_client);
        s_ctx->ws_client = NULL;
    }

    /* 停止并关闭录音 pipeline（下次 start 时重新 open，防止 AFE 空转） */
    if (s_ctx->rec_pipe) {
        record_pipe_stop(s_ctx->rec_pipe);
        record_pipe_close(s_ctx->rec_pipe);
        s_ctx->rec_pipe = NULL;
    }

    /* 关闭 Audio Render 流 */
    if (s_ctx->render_stream) {
        esp_audio_render_stream_close(s_ctx->render_stream);
    }

    xQueueReset(s_ctx->play_queue);
    xEventGroupClearBits(s_ctx->event_group, 0xFF);

    set_state(XIAOZHI_STATE_IDLE);
    ESP_LOGI(TAG, "Stopped");
    return ESP_OK;
}

esp_err_t xiaozhi_abort(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;
    if (s_ctx->state != XIAOZHI_STATE_SPEAKING &&
        s_ctx->state != XIAOZHI_STATE_LISTENING) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 清空播放队列 */
    opus_packet_t pkt;
    while (xQueueReceive(s_ctx->play_queue, &pkt, 0) == pdTRUE) {}

    send_abort_msg();
    s_ctx->recording_active = false;
    set_state(XIAOZHI_STATE_LISTENING);
    return ESP_OK;
}

xiaozhi_state_t xiaozhi_get_state(void)
{
    return s_ctx ? s_ctx->state : XIAOZHI_STATE_IDLE;
}

void xiaozhi_set_on_state(xiaozhi_on_state_t cb)
{
    if (s_ctx) s_ctx->on_state = cb;
}
void xiaozhi_set_on_stt(xiaozhi_on_stt_t cb)
{
    if (s_ctx) s_ctx->on_stt = cb;
}
void xiaozhi_set_on_tts_state(xiaozhi_on_tts_state_t cb)
{
    if (s_ctx) s_ctx->on_tts_state = cb;
}
void xiaozhi_set_on_llm(xiaozhi_on_llm_t cb)
{
    if (s_ctx) s_ctx->on_llm = cb;
}
void xiaozhi_set_on_message(xiaozhi_on_message_t cb)
{
    if (s_ctx) s_ctx->on_message = cb;
}
void xiaozhi_set_on_wakeup(xiaozhi_on_wakeup_t cb)
{
    if (s_ctx) s_ctx->on_wakeup = cb;
}

void xiaozhi_trigger_wakeup(void)
{
    if (!s_ctx) return;

    /* 通知 Python 层（on_wakeup 回调，通过 poll() 派发） */
    if (s_ctx->on_wakeup) {
        s_ctx->on_wakeup();
    }

    /* LISTENING 空闲状态下自动触发录音 */
    if (s_ctx->state == XIAOZHI_STATE_LISTENING && !s_ctx->recording_active) {
        ESP_LOGI(TAG, "Wakeup triggered, starting listen");
        xiaozhi_listen_start();
    }
}

esp_err_t xiaozhi_listen_start(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;

    xiaozhi_state_t cur = s_ctx->state;
    if (cur == XIAOZHI_STATE_SPEAKING) {
        /* 先中断 TTS */
        opus_packet_t pkt;
        while (xQueueReceive(s_ctx->play_queue, &pkt, 0) == pdTRUE) {}
        send_abort_msg();
        set_state(XIAOZHI_STATE_LISTENING);
    } else if (cur != XIAOZHI_STATE_LISTENING) {
        ESP_LOGW(TAG, "listen_start: invalid state %d (need LISTENING=%d)",
                 (int)cur, (int)XIAOZHI_STATE_LISTENING);
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx->recording_active = true;
    return send_listen_start();
}

esp_err_t xiaozhi_listen_stop(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;
    if (s_ctx->state != XIAOZHI_STATE_LISTENING) {
        ESP_LOGW(TAG, "listen_stop: not listening, state=%d", (int)s_ctx->state);
        return ESP_ERR_INVALID_STATE;
    }
    s_ctx->recording_active = false;
    return send_listen_stop();
}
