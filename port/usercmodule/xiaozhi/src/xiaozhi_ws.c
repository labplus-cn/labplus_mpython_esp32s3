/*
 * xiaozhi_ws.c - 小智AI WebSocket 连接管理层实现
 */

#include "xiaozhi_ws.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_websocket_client.h"

#define TAG "xz_ws"

#define DEFAULT_RECV_BUF_SIZE 8192

/* ====================================================
 * 内部数据结构
 * ==================================================== */

struct xiaozhi_ws_s {
    esp_websocket_client_handle_t client;

    /* 回调 */
    xiaozhi_ws_on_connected_t    on_connected;
    xiaozhi_ws_on_disconnected_t on_disconnected;
    xiaozhi_ws_on_binary_t       on_binary;
    xiaozhi_ws_on_text_t         on_text;
    xiaozhi_ws_on_error_t        on_error;
    void                        *cb_ctx;

    /* 配置副本（connect 时使用） */
    char url[256];
    char auth_hdr[300];
    char device_id[32];
    char client_id[64];
    int  recv_buf_size;
};

/* ====================================================
 * esp_websocket_client 事件处理
 * ==================================================== */

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    struct xiaozhi_ws_s *ws = (struct xiaozhi_ws_s *)handler_args;
    if (!ws) return;

    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        if (ws->on_connected) ws->on_connected(ws->cb_ctx);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WS disconnected");
        if (ws->on_disconnected) ws->on_disconnected(ws->cb_ctx);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!data) break;

        /* 关闭帧：记录状态码（辅助调试） */
        if (data->op_code == WS_TRANSPORT_OPCODES_CLOSE) {
            if (data->data_len >= 2) {
                uint16_t code = ((uint8_t)data->data_ptr[0] << 8)
                              | (uint8_t)data->data_ptr[1];
                ESP_LOGW(TAG, "← WS close: code=%u reason=%.*s",
                         code, data->data_len - 2, data->data_ptr + 2);
            } else {
                ESP_LOGW(TAG, "← WS close: no code");
            }
            break;
        }

        if (!data->data_ptr || data->data_len <= 0) break;

        if (data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
            if (ws->on_binary) {
                ws->on_binary((const uint8_t *)data->data_ptr,
                              data->data_len, ws->cb_ctx);
            }
        } else if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
            /* data_ptr 可能不以 null 结尾，复制后再回调 */
            char *buf = malloc(data->data_len + 1);
            if (buf) {
                memcpy(buf, data->data_ptr, data->data_len);
                buf[data->data_len] = '\0';
                if (ws->on_text) ws->on_text(buf, ws->cb_ctx);
                free(buf);
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        if (ws->on_error) ws->on_error(ws->cb_ctx);
        break;

    default:
        break;
    }
}

/* ====================================================
 * 公开 API 实现
 * ==================================================== */

esp_err_t xiaozhi_ws_init(const xiaozhi_ws_config_t *config,
                           xiaozhi_ws_handle_t *out_hdl)
{
    if (!config || !config->url || !config->token || !out_hdl) {
        return ESP_ERR_INVALID_ARG;
    }

    struct xiaozhi_ws_s *ws = calloc(1, sizeof(*ws));
    if (!ws) return ESP_ERR_NO_MEM;

    strncpy(ws->url, config->url, sizeof(ws->url) - 1);

    /* 构造 Authorization 头 */
    if (strchr(config->token, ' ') == NULL) {
        snprintf(ws->auth_hdr, sizeof(ws->auth_hdr), "Bearer %s", config->token);
    } else {
        strncpy(ws->auth_hdr, config->token, sizeof(ws->auth_hdr) - 1);
    }

    if (config->device_id) {
        strncpy(ws->device_id, config->device_id, sizeof(ws->device_id) - 1);
    }
    if (config->client_id) {
        strncpy(ws->client_id, config->client_id, sizeof(ws->client_id) - 1);
    }

    ws->recv_buf_size  = config->recv_buf_size > 0
                         ? config->recv_buf_size : DEFAULT_RECV_BUF_SIZE;
    ws->on_connected    = config->on_connected;
    ws->on_disconnected = config->on_disconnected;
    ws->on_binary       = config->on_binary;
    ws->on_text         = config->on_text;
    ws->on_error        = config->on_error;
    ws->cb_ctx          = config->cb_ctx;

    *out_hdl = ws;
    return ESP_OK;
}

esp_err_t xiaozhi_ws_deinit(xiaozhi_ws_handle_t hdl)
{
    if (!hdl) return ESP_ERR_INVALID_ARG;
    /* 调用方须先 disconnect */
    free(hdl);
    return ESP_OK;
}

esp_err_t xiaozhi_ws_connect(xiaozhi_ws_handle_t hdl)
{
    if (!hdl) return ESP_ERR_INVALID_ARG;
    struct xiaozhi_ws_s *ws = hdl;

    if (ws->client) {
        ESP_LOGW(TAG, "Already connected, call disconnect first");
        return ESP_ERR_INVALID_STATE;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri                    = ws->url,
        .buffer_size            = ws->recv_buf_size,
        .task_stack             = 8192,
        .task_prio              = 5,
        .reconnect_timeout_ms   = 10000,
        .network_timeout_ms     = 10000,
        .disable_auto_reconnect = true,
    };

    ws->client = esp_websocket_client_init(&ws_cfg);
    if (!ws->client) {
        ESP_LOGE(TAG, "WS client init failed");
        return ESP_FAIL;
    }

    esp_websocket_client_append_header(ws->client, "Authorization",    ws->auth_hdr);
    esp_websocket_client_append_header(ws->client, "Protocol-Version", "1");
    if (ws->device_id[0]) {
        esp_websocket_client_append_header(ws->client, "Device-Id", ws->device_id);
    }
    if (ws->client_id[0]) {
        esp_websocket_client_append_header(ws->client, "Client-Id", ws->client_id);
    }

    esp_websocket_register_events(ws->client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, ws);

    esp_err_t err = esp_websocket_client_start(ws->client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WS start failed: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(ws->client);
        ws->client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "WS connect initiated: %s", ws->url);
    return ESP_OK;
}

esp_err_t xiaozhi_ws_disconnect(xiaozhi_ws_handle_t hdl)
{
    if (!hdl) return ESP_ERR_INVALID_ARG;
    struct xiaozhi_ws_s *ws = hdl;

    if (ws->client) {
        esp_websocket_client_stop(ws->client);
        esp_websocket_client_destroy(ws->client);
        ws->client = NULL;
    }
    return ESP_OK;
}

esp_err_t xiaozhi_ws_send_text(xiaozhi_ws_handle_t hdl, const char *text)
{
    if (!hdl || !text) return ESP_ERR_INVALID_ARG;
    struct xiaozhi_ws_s *ws = hdl;

    if (!ws->client || !esp_websocket_client_is_connected(ws->client)) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_websocket_client_send_text(ws->client, text,
                                             (int)strlen(text),
                                             pdMS_TO_TICKS(2000));
    if (ret < 0) {
        ESP_LOGE(TAG, "send_text failed: %d", ret);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "→ text %d bytes", ret);
    return ESP_OK;
}

esp_err_t xiaozhi_ws_send_binary(xiaozhi_ws_handle_t hdl,
                                  const uint8_t *data, int len)
{
    if (!hdl || !data || len <= 0) return ESP_ERR_INVALID_ARG;
    struct xiaozhi_ws_s *ws = hdl;

    if (!ws->client || !esp_websocket_client_is_connected(ws->client)) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_websocket_client_send_bin(ws->client,
                                            (const char *)data, len,
                                            pdMS_TO_TICKS(10));
    if (ret < 0) {
        ESP_LOGW(TAG, "send_bin failed (drop frame): %d", ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool xiaozhi_ws_is_connected(xiaozhi_ws_handle_t hdl)
{
    if (!hdl) return false;
    struct xiaozhi_ws_s *ws = hdl;
    return ws->client && esp_websocket_client_is_connected(ws->client);
}
