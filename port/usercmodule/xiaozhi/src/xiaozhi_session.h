/*
 * xiaozhi_session.h - 小智AI语音会话 C 接口
 *
 * 提供连接小智AI服务器的语音会话功能，实现语音输入/输出、
 * 状态管理和 JSON 消息处理。
 *
 * 依赖:
 *   - esp_websocket_client (WebSocket 通信)
 *   - esp_audio_codec (Opus 编解码)
 *   - esp_codec_dev (音频硬件 I/O)
 *   - esp_gmf_app_utils (GMF 应用工具)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================
 * 状态枚举
 * ==================== */

typedef enum {
    XIAOZHI_STATE_IDLE       = 0,  /*!< 空闲，无连接 */
    XIAOZHI_STATE_CONNECTING = 1,  /*!< 正在连接服务器，等待握手 */
    XIAOZHI_STATE_LISTENING  = 2,  /*!< 正在收音并发送给服务器 */
    XIAOZHI_STATE_SPEAKING   = 3,  /*!< 正在播放服务器返回的语音 */
    XIAOZHI_STATE_ERROR      = 4,  /*!< 连接或运行出错 */
    XIAOZHI_STATE_CONNECTED  = 5,  /*!< 已连接服务器，等待唤醒/外部触发 */
} xiaozhi_state_t;

/* ====================
 * 回调函数类型
 * ==================== */

/** 状态变化回调 */
typedef void (*xiaozhi_on_state_t)(xiaozhi_state_t state);

/** 语音识别结果回调 (STT) */
typedef void (*xiaozhi_on_stt_t)(const char *text);

/**
 * TTS 状态回调
 * @param state  "start" / "stop" / "sentence_start"
 * @param text   sentence_start 时的文本，其它为 NULL
 */
typedef void (*xiaozhi_on_tts_state_t)(const char *state, const char *text);

/**
 * LLM 情感回调
 * @param emotion  情感标签，如 "happy" / "sad"
 * @param text     附带文本（可能为 NULL）
 */
typedef void (*xiaozhi_on_llm_t)(const char *emotion, const char *text);

/** 任意 JSON 消息原文回调（用于高级处理） */
typedef void (*xiaozhi_on_message_t)(const char *json_text);

/** 唤醒词检测回调（AFE 检测到唤醒词时触发） */
typedef void (*xiaozhi_on_wakeup_t)(void);

/* ====================
 * 配置结构
 * ==================== */

/**
 * 小智会话配置
 *
 * url, token 为必填项，其余为可选项（NULL 时使用默认值）。
 */
typedef struct {
    const char *url;              /*!< WebSocket URL, 如 "wss://api.tenclass.net/xiaozhi/v1/" */
    const char *token;            /*!< Bearer 令牌 */
    const char *device_id;        /*!< 设备 ID（NULL 时自动使用 WiFi MAC 地址） */
    const char *client_id;        /*!< 客户端 UUID（NULL 时自动生成） */
    int         frame_duration_ms;/*!< 音频帧时长（ms），0 = 使用默认值 60ms */
    int         output_volume;    /*!< 扬声器音量 0-100，0 = 使用默认值 80 */
    float       input_gain_db;    /*!< 麦克风增益（dB），0.0 = 使用默认值 35.0 */
} xiaozhi_config_t;

/* ====================
 * 公开 API
 * ==================== */

/**
 * 初始化小智会话模块
 *
 * 必须在调用其他 API 之前调用。
 * 会初始化音频硬件（codec），可与 audio.init() 共存但不应重复调用。
 *
 * @param config  会话配置，url 和 token 为必填
 * @return  ESP_OK 成功
 */
esp_err_t xiaozhi_init(const xiaozhi_config_t *config);

/**
 * 释放小智会话资源
 *
 * 调用前应先调用 xiaozhi_stop()。
 *
 * @return  ESP_OK 成功
 */
esp_err_t xiaozhi_deinit(void);

/**
 * 启动语音会话
 *
 * 连接 WebSocket 服务器，完成握手后进入 LISTENING 状态。
 * 可在 IDLE 或 ERROR 状态时调用。
 *
 * @return  ESP_OK 成功启动连接流程（异步，通过 on_state 回调通知结果）
 */
esp_err_t xiaozhi_start(void);

/**
 * 停止语音会话
 *
 * 断开 WebSocket 连接，停止音频任务，回到 IDLE 状态。
 *
 * @return  ESP_OK 成功
 */
esp_err_t xiaozhi_stop(void);

/**
 * 中断当前说话（TTS 播放）
 *
 * 可在 SPEAKING 状态下调用，终止当前 TTS 并切换为 LISTENING。
 *
 * @return  ESP_OK 成功
 */
esp_err_t xiaozhi_abort(void);

/**
 * 开始监听（发送语音）
 *
 * 可在 CONNECTED 状态下调用，切换为 LISTENING 并向服务器发送 listen start。
 * 也可在 SPEAKING 状态下调用（相当于先 abort 再 listen）。
 *
 * @return  ESP_OK 成功
 */
esp_err_t xiaozhi_listen_start(void);

/**
 * 停止监听
 *
 * 可在 LISTENING 状态下调用，切换为 CONNECTED 并向服务器发送 listen stop。
 *
 * @return  ESP_OK 成功
 */
esp_err_t xiaozhi_listen_stop(void);

/**
 * 获取当前状态
 */
xiaozhi_state_t xiaozhi_get_state(void);

/* ====================
 * 回调注册
 * ==================== */

/** 注册状态变化回调 */
void xiaozhi_set_on_state(xiaozhi_on_state_t cb);

/** 注册语音识别结果回调 */
void xiaozhi_set_on_stt(xiaozhi_on_stt_t cb);

/** 注册 TTS 状态回调 */
void xiaozhi_set_on_tts_state(xiaozhi_on_tts_state_t cb);

/** 注册 LLM 情感回调 */
void xiaozhi_set_on_llm(xiaozhi_on_llm_t cb);

/** 注册原始 JSON 消息回调 */
void xiaozhi_set_on_message(xiaozhi_on_message_t cb);

/** 注册唤醒词检测回调 */
void xiaozhi_set_on_wakeup(xiaozhi_on_wakeup_t cb);

/**
 * 设置唤醒词模式
 *
 * enable=true:  xiaozhi_start() 连接后进入 CONNECTED（等待唤醒），
 *               TTS 结束后也回到 CONNECTED。
 * enable=false: 默认行为，连接后直接进入 LISTENING，TTS 结束后回到 LISTENING。
 *
 * 必须在 xiaozhi_init() 之后、xiaozhi_start() 之前调用。
 */
void xiaozhi_set_wakeup_mode(bool enable);

/**
 * 触发唤醒事件
 *
 * 供 AFE 检测到唤醒词后调用，或由 Python 层（按钮等）直接触发。
 * 会调用已注册的 on_wakeup 回调（通过 poll() 派发到 Python）。
 * 在唤醒词模式下，通常在回调里调用 xiaozhi_listen_start()。
 */
void xiaozhi_trigger_wakeup(void);

#ifdef __cplusplus
}
#endif
