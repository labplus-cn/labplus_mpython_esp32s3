/*
 * xiaozhi_ws.h - 小智AI WebSocket 连接管理层
 *
 * 封装 esp_websocket_client 的生命周期管理和消息收发：
 *   - 连接/断开 WebSocket
 *   - 发送文本（JSON）和二进制（Opus 帧）
 *   - 接收事件回调（连接/断开/数据/错误）
 *
 * 上层（session）通过注册回调获取事件通知，
 * 无需直接操作 esp_websocket_client_handle_t。
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xiaozhi_ws_s *xiaozhi_ws_handle_t;

/* ====================================================
 * 事件回调类型
 * ==================================================== */

/** WebSocket 已连接（TCP + WS 握手完成） */
typedef void (*xiaozhi_ws_on_connected_t)(void *ctx);

/** WebSocket 已断开 */
typedef void (*xiaozhi_ws_on_disconnected_t)(void *ctx);

/**
 * 收到二进制帧（Opus 音频）
 * @param data  帧数据指针（仅在回调内有效）
 * @param len   帧长度（字节）
 * @param ctx   用户上下文
 */
typedef void (*xiaozhi_ws_on_binary_t)(const uint8_t *data, int len, void *ctx);

/**
 * 收到文本帧（JSON 控制消息）
 * @param text  以 '\0' 结尾的 JSON 字符串（仅在回调内有效）
 * @param ctx   用户上下文
 */
typedef void (*xiaozhi_ws_on_text_t)(const char *text, void *ctx);

/** WebSocket 错误 */
typedef void (*xiaozhi_ws_on_error_t)(void *ctx);

/* ====================================================
 * 配置结构
 * ==================================================== */

typedef struct {
    const char *url;          /*!< WebSocket URL */
    const char *token;        /*!< Bearer 令牌（自动添加 "Bearer " 前缀，除非已含空格） */
    const char *device_id;    /*!< Device-Id 请求头 */
    const char *client_id;    /*!< Client-Id 请求头 */
    int         recv_buf_size;/*!< 接收缓冲区大小，0 使用默认值 8192 */

    /* 事件回调（均可为 NULL） */
    xiaozhi_ws_on_connected_t    on_connected;
    xiaozhi_ws_on_disconnected_t on_disconnected;
    xiaozhi_ws_on_binary_t       on_binary;
    xiaozhi_ws_on_text_t         on_text;
    xiaozhi_ws_on_error_t        on_error;
    void                        *cb_ctx; /*!< 传递给所有回调的用户上下文 */
} xiaozhi_ws_config_t;

/* ====================================================
 * API
 * ==================================================== */

/**
 * 创建 WebSocket 客户端（不发起连接）。
 *
 * @param config   配置，url/token 为必填
 * @param out_hdl  输出句柄
 * @return ESP_OK 成功
 */
esp_err_t xiaozhi_ws_init(const xiaozhi_ws_config_t *config,
                           xiaozhi_ws_handle_t *out_hdl);

/**
 * 释放 WebSocket 客户端资源。
 *
 * 调用前须先调用 xiaozhi_ws_disconnect()。
 */
esp_err_t xiaozhi_ws_deinit(xiaozhi_ws_handle_t hdl);

/**
 * 发起 WebSocket 连接。
 *
 * 非阻塞：连接结果通过 on_connected / on_disconnected / on_error 回调通知。
 *
 * @return ESP_OK 表示连接请求已发出（不代表连接成功）
 */
esp_err_t xiaozhi_ws_connect(xiaozhi_ws_handle_t hdl);

/**
 * 断开 WebSocket 连接并销毁底层客户端。
 */
esp_err_t xiaozhi_ws_disconnect(xiaozhi_ws_handle_t hdl);

/**
 * 发送文本帧（JSON 消息）。
 *
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 未连接
 */
esp_err_t xiaozhi_ws_send_text(xiaozhi_ws_handle_t hdl, const char *text);

/**
 * 发送二进制帧（Opus 音频）。
 *
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 未连接
 */
esp_err_t xiaozhi_ws_send_binary(xiaozhi_ws_handle_t hdl,
                                  const uint8_t *data, int len);

/**
 * 查询当前是否已连接。
 */
bool xiaozhi_ws_is_connected(xiaozhi_ws_handle_t hdl);

#ifdef __cplusplus
}
#endif
