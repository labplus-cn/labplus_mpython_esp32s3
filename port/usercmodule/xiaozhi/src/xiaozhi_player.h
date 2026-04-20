/*
 * xiaozhi_player.h - 小智AI播放器子系统接口
 *
 * 封装音频播放所需的全部硬件资源：
 *   - Opus 解码器（esp_opus_dec）
 *   - GMF Pool + esp_audio_render（mono→stereo 格式转换）
 *   - playback_task（播放队列消费者）
 *
 * 数据流：
 *   WebSocket 二进制帧 → xiaozhi_player_push() 入队
 *   → playback_task → Opus 解码 → esp_audio_render → 扬声器
 *
 * 音频约定（与 session 侧保持一致）：
 *   - 输入：Opus，16kHz，mono，60ms 帧
 *   - 输出：PCM，16kHz→扬声器（render 内部完成 mono→stereo 转换）
 */

#pragma once

#include "esp_err.h"
#include "esp_codec_dev.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xiaozhi_player_s *xiaozhi_player_handle_t;

/**
 * 播放器配置
 */
typedef struct {
    esp_codec_dev_handle_t play_dev;    /*!< 播放 codec 设备句柄（必须已初始化） */
    int                    sample_rate; /*!< 硬件输出采样率，Hz（通常 16000） */
    int                    channels;    /*!< 硬件输出声道数（通常 2，stereo） */
    int                    bits;        /*!< 位深（通常 16） */
    int                    frame_ms;    /*!< Opus 帧时长，ms（通常 60） */
    int                    queue_size;  /*!< 播放队列深度（帧数），0 使用默认值 20 */
} xiaozhi_player_config_t;

/**
 * 创建并初始化播放器。
 *
 * 完成 Opus 解码器、GMF Pool、esp_audio_render 的初始化，
 * 但不启动播放任务（需在 xiaozhi_player_start() 后调用）。
 *
 * @param config   播放器配置，play_dev 为必填
 * @param out_hdl  输出句柄
 * @return ESP_OK 成功
 */
esp_err_t xiaozhi_player_init(const xiaozhi_player_config_t *config,
                               xiaozhi_player_handle_t *out_hdl);

/**
 * 释放播放器所有资源。
 *
 * 调用前须先调用 xiaozhi_player_stop()。
 */
esp_err_t xiaozhi_player_deinit(xiaozhi_player_handle_t hdl);

/**
 * 打开 Audio Render 流并启动 playback_task。
 *
 * 须在 WebSocket 握手完成（会话就绪）后调用。
 */
esp_err_t xiaozhi_player_start(xiaozhi_player_handle_t hdl);

/**
 * 停止 playback_task 并关闭 Audio Render 流。
 *
 * 等待任务退出（最多 2s），超时后强制删除。
 */
esp_err_t xiaozhi_player_stop(xiaozhi_player_handle_t hdl);

/**
 * 将一帧 Opus 数据推入播放队列（由 WS 接收方调用）。
 *
 * 队列满时丢弃该帧并打印警告。
 *
 * @param hdl   播放器句柄
 * @param data  Opus 帧数据指针
 * @param len   帧数据长度（字节）
 */
void xiaozhi_player_push(xiaozhi_player_handle_t hdl,
                          const uint8_t *data, int len);

/**
 * 清空播放队列（中断/abort 时使用）。
 */
void xiaozhi_player_flush(xiaozhi_player_handle_t hdl);

/**
 * 通知播放器 TTS 已结束（服务端发完最后一帧）。
 *
 * 播放器在队列排空后自动调用 on_drained 回调，
 * session 层可借此切换状态。
 */
void xiaozhi_player_notify_tts_stop(xiaozhi_player_handle_t hdl);

/**
 * 注册"队列排空"回调。
 *
 * 在收到 tts_stop 通知且播放队列已排空（静默 200ms）后触发。
 *
 * @param hdl       播放器句柄
 * @param cb        回调函数指针（可为 NULL，取消注册）
 * @param cb_ctx    传递给回调的用户上下文
 */
typedef void (*xiaozhi_player_drained_cb_t)(void *cb_ctx);
void xiaozhi_player_set_on_drained(xiaozhi_player_handle_t hdl,
                                    xiaozhi_player_drained_cb_t cb,
                                    void *cb_ctx);

#ifdef __cplusplus
}
#endif
