/*
 * xiaozhi_session.c - 小智AI语音会话核心实现
 *
 * 本文件只关注会话逻辑：
 *   - 连接/断开 WebSocket（通过 xiaozhi_ws 子模块）
 *   - Hello 握手协议
 *   - 音频捕获（录音 pipeline → WebSocket 发送）任务
 *   - JSON 消息处理（STT / TTS / LLM / system / mcp 事件）
 *   - 状态机管理
 *   - 公开 API：init / deinit / start / stop / abort / listen_start / listen_stop
 *
 * 音频播放由 xiaozhi_player 子模块负责：
 *   WebSocket 二进制帧 → xiaozhi_player_push() → Opus 解码 → 扬声器
 *
 * WebSocket 连接管理由 xiaozhi_ws 子模块负责。
 *
 * 前置条件:
 *   调用 xiaozhi_init() 之前，必须先完成音频硬件初始化
 *   （在 MicroPython 层调用 audio.init() 或在 C 层调用
 *    esp_gmf_app_setup_codec_dev()）。
 */

#include "xiaozhi_session.h"
#include "xiaozhi_player.h"
#include "xiaozhi_ws.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

/* GMF 音频硬件（获取 codec 句柄） */
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_codec_dev.h"

/* 录音 pipeline 封装 */
#include "audio_capture.h"

/* ====================================================
 * 常量和宏定义
 * ==================================================== */

#define TAG "xiaozhi"

/* 音频硬件参数 */
#define AUDIO_SAMPLE_RATE    16000
#define AUDIO_CHANNELS       2   /* 硬件为 stereo */
#define AUDIO_BITS           16
#define OPUS_SAMPLE_RATE     16000
#define OPUS_CHANNELS        1   /* Opus mono */
#define OPUS_FRAME_MS        60

/* WebSocket 接收缓冲区 */
#define WS_RECV_BUF_SIZE     8192

/* FreeRTOS 事件位 */
#define EVT_CONNECTED        (1 << 0)  /* 收到服务器 hello，握手完成 */
#define EVT_DISCONNECTED     (1 << 1)  /* WebSocket 断开 */
#define EVT_STOP_REQUESTED   (1 << 2)  /* 上层请求停止 */

/* 音频捕获任务参数 */
#define CAPTURE_TASK_STACK   6144
#define CAPTURE_TASK_PRIO    7

/* ====================================================
 * 全局会话状态
 * ==================================================== */

typedef struct {
    /* 配置 */
    char url[256];
    char token[256];
    char device_id[32];
    char client_id[64];
    int  frame_ms;

    /* 状态 */
    volatile xiaozhi_state_t state;
    volatile bool recording_active;  /* true=用户已触发录音（listen_start） */
    volatile bool vad_active;        /* true=AFE VAD 检测到语音；无 AFE 时恒为 true */
    char session_id[64];
    bool hello_received;

    /* 子模块句柄 */
    xiaozhi_ws_handle_t     ws;
    xiaozhi_player_handle_t player;

    /* 录音设备句柄 */
    esp_codec_dev_handle_t rec_dev;

    /* 录音 pipeline */
    record_pipe_handle_t rec_pipe;

    /* FreeRTOS 同步 */
    EventGroupHandle_t event_group;
    SemaphoreHandle_t  state_mutex;

    /* 音频捕获任务句柄 */
    TaskHandle_t capture_task;

    /* 用户回调 */
    xiaozhi_on_state_t     on_state;
    xiaozhi_on_stt_t       on_stt;
    xiaozhi_on_tts_state_t on_tts_state;
    xiaozhi_on_llm_t       on_llm;
    xiaozhi_on_message_t   on_message;
    xiaozhi_on_wakeup_t    on_wakeup;
    xiaozhi_on_mcp_t       on_mcp;

} xiaozhi_ctx_t;

static xiaozhi_ctx_t *s_ctx = NULL;

/* ====================================================
 * 内部函数声明
 * ==================================================== */

static void set_state(xiaozhi_state_t new_state);
static esp_err_t send_hello(void);
static esp_err_t send_listen_start(void);
static esp_err_t send_listen_stop(void);
static esp_err_t send_abort_msg(void);
static void handle_json_message(const char *json_str);
static void capture_task_fn(void *arg);
static void get_mac_str(char *buf, size_t len);
static void session_wakeup_cb(void *ctx);
static void session_vad_cb(record_pipe_vad_event_t event, void *ctx);

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
 * JSON 消息发送（通过 xiaozhi_ws）
 * ==================================================== */

static esp_err_t send_hello(void)
{
    cJSON *root     = cJSON_CreateObject();
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
    esp_err_t err = xiaozhi_ws_send_text(s_ctx->ws, str);
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
    esp_err_t err = xiaozhi_ws_send_text(s_ctx->ws, str);
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

    esp_err_t err = xiaozhi_ws_send_text(s_ctx->ws, str);
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

    esp_err_t err = xiaozhi_ws_send_text(s_ctx->ws, str);
    free(str);
    return err;
}

/* ====================================================
 * JSON 消息接收处理
 * ==================================================== */

static void handle_json_message(const char *json_str)
{
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
            set_state(XIAOZHI_STATE_LISTENING);
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
            /* 服务器开始 TTS：flush 播放队列，切换为 SPEAKING；
             * recording_active 保持不变——state != SPEAKING 已阻止发帧，
             * TTS 结束后无需重新触发即可继续多轮会话 */
            xiaozhi_player_flush(s_ctx->player);
            set_state(XIAOZHI_STATE_SPEAKING);
        } else if (strcmp(tts_state, "stop") == 0) {
            xiaozhi_player_notify_tts_stop(s_ctx->player);
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
    /* ---- mcp ---- */
    else if (strcmp(type, "mcp") == 0) {
        cJSON *payload_j = cJSON_GetObjectItem(root, "payload");
        if (payload_j && s_ctx->on_mcp) {
            char *req_str = cJSON_PrintUnformatted(payload_j);
            if (req_str) {
                char *resp_buf = malloc(WS_RECV_BUF_SIZE);
                if (resp_buf) {
                    if (s_ctx->on_mcp(req_str, resp_buf, WS_RECV_BUF_SIZE)) {
                        cJSON *resp_root = cJSON_CreateObject();
                        if (s_ctx->session_id[0]) {
                            cJSON_AddStringToObject(resp_root, "session_id",
                                                    s_ctx->session_id);
                        }
                        cJSON_AddStringToObject(resp_root, "type", "mcp");
                        cJSON *resp_payload = cJSON_Parse(resp_buf);
                        if (resp_payload) {
                            cJSON_AddItemToObject(resp_root, "payload", resp_payload);
                            char *resp_str = cJSON_PrintUnformatted(resp_root);
                            if (resp_str) {
                                xiaozhi_ws_send_text(s_ctx->ws, resp_str);
                                free(resp_str);
                            }
                        }
                        cJSON_Delete(resp_root);
                    }
                    free(resp_buf);
                }
                free(req_str);
            }
        }
    }

    cJSON_Delete(root);
}

/* ====================================================
 * xiaozhi_ws 事件回调（由 ws 子模块调用）
 * ==================================================== */

static void on_ws_connected(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "WS connected, sending hello");
    set_state(XIAOZHI_STATE_CONNECTED);
    if (send_hello() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send hello");
        xEventGroupSetBits(s_ctx->event_group, EVT_DISCONNECTED);
    }
}

static void on_ws_disconnected(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "WS disconnected");
    xEventGroupSetBits(s_ctx->event_group, EVT_DISCONNECTED);
    set_state(XIAOZHI_STATE_IDLE);
}

static void on_ws_binary(const uint8_t *data, int len, void *ctx)
{
    (void)ctx;
    /* 仅 SPEAKING 状态下将 Opus 帧推入播放器 */
    if (s_ctx->state == XIAOZHI_STATE_SPEAKING) {
        xiaozhi_player_push(s_ctx->player, data, len);
    }
}

static void on_ws_text(const char *text, void *ctx)
{
    (void)ctx;
    handle_json_message(text);
}

static void on_ws_error(void *ctx)
{
    (void)ctx;
    ESP_LOGE(TAG, "WS error");
    xEventGroupSetBits(s_ctx->event_group, EVT_DISCONNECTED);
    set_state(XIAOZHI_STATE_ERROR);
}

/* ====================================================
 * xiaozhi_player 排空回调
 * ==================================================== */

static void on_player_drained(void *ctx)
{
    (void)ctx;
    if (!s_ctx) return;
    ESP_LOGI(TAG, "Player drained, resume listening (multi-turn)");
    /* TTS 播完：recording_active 仍为 true，重置 VAD 门后直接恢复监听；
     * 向服务器发 listen:start 告知本轮开始，等待用户继续说话 */
    s_ctx->vad_active = true;
    set_state(XIAOZHI_STATE_LISTENING);
    send_listen_start();
}

/* ====================================================
 * 音频捕获任务：录音 pipeline Opus 帧 → WebSocket
 * ==================================================== */

static void session_wakeup_cb(void *ctx)
{
    (void)ctx;
    xiaozhi_trigger_wakeup();
}

static void session_vad_cb(record_pipe_vad_event_t event, void *ctx)
{
    (void)ctx;
    if (!s_ctx) return;
    if (event == RECORD_PIPE_VAD_START) {
        ESP_LOGD(TAG, "VAD: speech start → upload");
        s_ctx->vad_active = true;
    } else {
        ESP_LOGD(TAG, "VAD: speech stop → pause upload");
        s_ctx->vad_active = false;
    }
}

static void capture_task_fn(void *arg)
{
    /* vad_active 由 xiaozhi_listen_start() 在每次触发时置位：
     *   - 有 AFE + 唤醒词触发：listen_start 置 true，后续由 VAD 事件动态控制
     *   - 有 AFE + 手动触发：listen_start 置 true 并保持（AFE 在 ST_IDLE，无 VAD 事件）
     *   - 无 AFE（回退 pipeline）：listen_start 置 true 并保持（无 VAD 回调） */

    if (record_pipe_open(s_ctx->rec_dev,
                         session_wakeup_cb, NULL,
                         session_vad_cb, NULL,
                         &s_ctx->rec_pipe) != ESP_OK) {
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

        bool cur_recording = s_ctx->recording_active;

        const uint8_t *data = NULL;
        int size = 0;
        record_pipe_acquire_frame(s_ctx->rec_pipe, &data, &size);

        if (size <= 0) {
            vTaskDelay(pdMS_TO_TICKS(s_ctx->frame_ms));
        } else {
            /* 将帧复制到栈缓冲，立即释放 pipeline 缓冲 */
            uint8_t frame_buf[512];
            int send_size = (size <= (int)sizeof(frame_buf)) ? size : (int)sizeof(frame_buf);
            if (send_size > 0) {
                memcpy(frame_buf, data, send_size);
            }
            record_pipe_release_frame(s_ctx->rec_pipe);

            /* recording_active=true 且 VAD 检测到语音 且非 SPEAKING 时，发送 Opus 帧 */
            if (send_size > 0 &&
                cur_recording &&
                s_ctx->vad_active &&
                s_ctx->state != XIAOZHI_STATE_SPEAKING) {
                xiaozhi_ws_send_binary(s_ctx->ws, frame_buf, send_size);
            }
        }
    }

    /* 退出：停止并关闭录音 pipeline */
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

    strncpy(s_ctx->url,   config->url,   sizeof(s_ctx->url)   - 1);
    strncpy(s_ctx->token, config->token, sizeof(s_ctx->token) - 1);
    s_ctx->frame_ms = (config->frame_duration_ms > 0) ? config->frame_duration_ms : 60;

    /* 设备 ID */
    if (config->device_id && config->device_id[0]) {
        strncpy(s_ctx->device_id, config->device_id, sizeof(s_ctx->device_id) - 1);
    } else {
        get_mac_str(s_ctx->device_id, sizeof(s_ctx->device_id));
    }

    /* 客户端 UUID：从 NVS board/uuid 读取 */
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

    ESP_LOGI(TAG, "Config: url=%s device_id=%s client_id=%s frame_ms=%d",
             s_ctx->url, s_ctx->device_id, s_ctx->client_id, s_ctx->frame_ms);

    /* 获取音频硬件句柄 */
    esp_codec_dev_handle_t rec_dev  = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    esp_codec_dev_handle_t play_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();

    if (!rec_dev || !play_dev) {
        ESP_LOGE(TAG, "Codec device not ready. Call audio.init() first.");
        free(s_ctx); s_ctx = NULL;
        return ESP_ERR_INVALID_STATE;
    }
    s_ctx->rec_dev = rec_dev;

    if (config->output_volume > 0) {
        esp_codec_dev_set_out_vol(play_dev, config->output_volume);
    }
    if (config->input_gain_db > 0.0f) {
        esp_codec_dev_set_in_gain(rec_dev, config->input_gain_db);
    }

    /* 初始化 WebSocket 子模块 */
    xiaozhi_ws_config_t ws_cfg = {
        .url              = s_ctx->url,
        .token            = s_ctx->token,
        .device_id        = s_ctx->device_id,
        .client_id        = s_ctx->client_id,
        .recv_buf_size    = WS_RECV_BUF_SIZE,
        .on_connected     = on_ws_connected,
        .on_disconnected  = on_ws_disconnected,
        .on_binary        = on_ws_binary,
        .on_text          = on_ws_text,
        .on_error         = on_ws_error,
        .cb_ctx           = NULL,
    };
    if (xiaozhi_ws_init(&ws_cfg, &s_ctx->ws) != ESP_OK) {
        ESP_LOGE(TAG, "WS init failed");
        free(s_ctx); s_ctx = NULL;
        return ESP_FAIL;
    }

    /* 初始化播放器子模块 */
    xiaozhi_player_config_t player_cfg = {
        .play_dev    = play_dev,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .channels    = AUDIO_CHANNELS,
        .bits        = 16,
        .frame_ms    = s_ctx->frame_ms,
        .queue_size  = 20,
    };
    if (xiaozhi_player_init(&player_cfg, &s_ctx->player) != ESP_OK) {
        ESP_LOGE(TAG, "Player init failed");
        xiaozhi_ws_deinit(s_ctx->ws);
        free(s_ctx); s_ctx = NULL;
        return ESP_FAIL;
    }
    xiaozhi_player_set_on_drained(s_ctx->player, on_player_drained, NULL);

    /* FreeRTOS 同步对象 */
    s_ctx->event_group = xEventGroupCreate();
    s_ctx->state_mutex = xSemaphoreCreateMutex();

    if (!s_ctx->event_group || !s_ctx->state_mutex) {
        ESP_LOGE(TAG, "FreeRTOS object creation failed");
        if (s_ctx->event_group) vEventGroupDelete(s_ctx->event_group);
        if (s_ctx->state_mutex) vSemaphoreDelete(s_ctx->state_mutex);
        xiaozhi_player_deinit(s_ctx->player);
        xiaozhi_ws_deinit(s_ctx->ws);
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

    xiaozhi_stop();

    if (s_ctx->rec_pipe) {
        record_pipe_close(s_ctx->rec_pipe);
        s_ctx->rec_pipe = NULL;
    }

    xiaozhi_player_deinit(s_ctx->player);
    xiaozhi_ws_deinit(s_ctx->ws);

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
    s_ctx->hello_received   = false;
    s_ctx->recording_active = false;
    memset(s_ctx->session_id, 0, sizeof(s_ctx->session_id));

    /* 发起 WebSocket 连接（非阻塞） */
    esp_err_t err = xiaozhi_ws_connect(s_ctx->ws);
    if (err != ESP_OK) {
        set_state(XIAOZHI_STATE_ERROR);
        return err;
    }

    /* 等待服务器 hello 响应（10s） */
    EventBits_t bits = xEventGroupWaitBits(s_ctx->event_group,
                                           EVT_CONNECTED | EVT_DISCONNECTED,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (!(bits & EVT_CONNECTED) || (bits & EVT_DISCONNECTED)) {
        ESP_LOGE(TAG, "Server hello timeout or disconnected");
        xiaozhi_ws_disconnect(s_ctx->ws);
        set_state(XIAOZHI_STATE_ERROR);
        return ESP_ERR_TIMEOUT;
    }

    /* 启动播放器 */
    if (xiaozhi_player_start(s_ctx->player) != ESP_OK) {
        ESP_LOGE(TAG, "Player start failed");
        xiaozhi_ws_disconnect(s_ctx->ws);
        set_state(XIAOZHI_STATE_ERROR);
        return ESP_FAIL;
    }

    /* 启动音频捕获任务（任务内部负责 record_pipe open+start） */
    xTaskCreatePinnedToCore(capture_task_fn, "xz_cap", CAPTURE_TASK_STACK,
                            NULL, CAPTURE_TASK_PRIO, &s_ctx->capture_task, 1);

    ESP_LOGI(TAG, "Session started, waiting for trigger");
    return ESP_OK;
}

esp_err_t xiaozhi_stop(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;
    if (s_ctx->state == XIAOZHI_STATE_IDLE) return ESP_OK;

    ESP_LOGI(TAG, "Stopping");

    xEventGroupSetBits(s_ctx->event_group, EVT_STOP_REQUESTED);

    /* 先停止录音 pipeline，让 capture_task 尽快退出 acquire_frame 阻塞 */
    if (s_ctx->rec_pipe) {
        record_pipe_stop(s_ctx->rec_pipe);
    }

    /* 发送 goodbye */
    if (xiaozhi_ws_is_connected(s_ctx->ws)) {
        send_listen_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* 等待 capture_task 退出（最多 2s） */
    int timeout_ms = 2000;
    while (s_ctx->capture_task && timeout_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        timeout_ms -= 50;
    }
    if (s_ctx->capture_task) {
        vTaskDelete(s_ctx->capture_task);
        s_ctx->capture_task = NULL;
    }

    /* 停止播放器 */
    xiaozhi_player_stop(s_ctx->player);

    /* 断开 WebSocket */
    xiaozhi_ws_disconnect(s_ctx->ws);

    /* 关闭录音 pipeline（stop 已在上方调用，此处只需 close） */
    if (s_ctx->rec_pipe) {
        record_pipe_close(s_ctx->rec_pipe);
        s_ctx->rec_pipe = NULL;
    }

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
    xiaozhi_player_flush(s_ctx->player);
    send_abort_msg();
    s_ctx->recording_active = false;
    set_state(XIAOZHI_STATE_LISTENING);
    return ESP_OK;
}

xiaozhi_state_t xiaozhi_get_state(void)
{
    return s_ctx ? s_ctx->state : XIAOZHI_STATE_IDLE;
}

void xiaozhi_set_on_state(xiaozhi_on_state_t cb)     { if (s_ctx) s_ctx->on_state    = cb; }
void xiaozhi_set_on_stt(xiaozhi_on_stt_t cb)         { if (s_ctx) s_ctx->on_stt      = cb; }
void xiaozhi_set_on_tts_state(xiaozhi_on_tts_state_t cb) { if (s_ctx) s_ctx->on_tts_state = cb; }
void xiaozhi_set_on_llm(xiaozhi_on_llm_t cb)         { if (s_ctx) s_ctx->on_llm      = cb; }
void xiaozhi_set_on_message(xiaozhi_on_message_t cb) { if (s_ctx) s_ctx->on_message  = cb; }
void xiaozhi_set_on_wakeup(xiaozhi_on_wakeup_t cb)   { if (s_ctx) s_ctx->on_wakeup   = cb; }
void xiaozhi_set_on_mcp(xiaozhi_on_mcp_t cb)         { if (s_ctx) s_ctx->on_mcp      = cb; }

void xiaozhi_trigger_wakeup(void)
{
    if (!s_ctx) return;

    if (s_ctx->on_wakeup) {
        s_ctx->on_wakeup();
    }

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
        xiaozhi_player_flush(s_ctx->player);
        send_abort_msg();
        set_state(XIAOZHI_STATE_LISTENING);
    } else if (cur != XIAOZHI_STATE_LISTENING) {
        ESP_LOGW(TAG, "listen_start: invalid state %d (need LISTENING=%d)",
                 (int)cur, (int)XIAOZHI_STATE_LISTENING);
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx->vad_active = true;        /* 重置 VAD 门，避免遗留 false 丢弃首帧 */
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
