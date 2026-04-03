/*
 * audio_capture.h - GMF Capture pipeline 封装
 *
 * 管理录音 pipeline：麦克风（stereo）→ AFE 处理（唤醒词检测 + 降噪）→ Opus 编码
 * 向调用方提供 Opus 帧流。
 *
 * AFE（Audio Front End）集成：
 *   - 使用 esp_gmf_afe 元素在 pipeline 内完成 stereo→mono 转换和 WakeNet 唤醒词检测
 *   - 检测到唤醒词时调用 wakeup_cb 回调
 *   - 若 flash 中未找到 SR 模型，自动回退到无 AFE 的简单 pipeline
 *
 * 使用方式：
 *   1. record_pipe_open()  - 初始化 pipeline（不启动采集）
 *   2. record_pipe_start() - 开始从麦克风采集
 *   3. 循环调用 record_pipe_acquire_frame() / record_pipe_release_frame()
 *   4. record_pipe_stop()  - 停止采集
 *   5. record_pipe_close() - 释放资源
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct record_pipe_s *record_pipe_handle_t;

/**
 * 唤醒词检测回调类型。
 * 当 AFE 检测到唤醒词时从 AFE fetch 任务中调用。
 *
 * @param ctx  用户自定义上下文（传入 record_pipe_open 的 wakeup_ctx）
 */
typedef void (*record_pipe_wakeup_cb_t)(void *ctx);

/**
 * VAD（语音活动检测）事件类型。
 */
typedef enum {
    RECORD_PIPE_VAD_START = 0,  /*!< 检测到语音开始 */
    RECORD_PIPE_VAD_STOP  = 1,  /*!< 检测到语音结束（静音） */
} record_pipe_vad_event_t;

/**
 * VAD 事件回调类型。
 * 仅在 AFE 模式下有效（有 SR 模型时）；回退模式下不会调用。
 *
 * @param event  VAD_START 或 VAD_STOP
 * @param ctx    用户自定义上下文（传入 record_pipe_open 的 vad_ctx）
 */
typedef void (*record_pipe_vad_cb_t)(record_pipe_vad_event_t event, void *ctx);

/**
 * 创建并初始化录音 pipeline。
 *
 * 若 flash 分区 "model" 中存在 WakeNet 模型且 wakeup_cb 非 NULL，
 * pipeline 将集成 AFE 唤醒词检测 + VAD：麦克风 → [ai_afe] → [aud_enc]
 * 否则回退到简单模式：麦克风 → [aud_ch_cvt] → [aud_enc]（VAD 不可用）
 *
 * @param rec_dev     录音 codec 设备句柄（必须已完成初始化）
 * @param wakeup_cb   唤醒词检测回调（可为 NULL，表示不使用 AFE 唤醒）
 * @param wakeup_ctx  传递给唤醒回调的用户上下文
 * @param vad_cb      VAD 事件回调（可为 NULL，表示不使用 VAD 门控）
 * @param vad_ctx     传递给 VAD 回调的用户上下文
 * @param out_rp      输出句柄
 * @return ESP_OK 成功
 */
esp_err_t record_pipe_open(esp_codec_dev_handle_t rec_dev,
                            record_pipe_wakeup_cb_t wakeup_cb, void *wakeup_ctx,
                            record_pipe_vad_cb_t vad_cb, void *vad_ctx,
                            record_pipe_handle_t *out_rp);

/**
 * 启动录音（开始从麦克风采集 PCM，pipeline 开始运行）。
 */
esp_err_t record_pipe_start(record_pipe_handle_t rp);

/**
 * 停止录音（停止 pipeline，麦克风停止采集）。
 */
esp_err_t record_pipe_stop(record_pipe_handle_t rp);

/**
 * 销毁录音 pipeline，释放所有资源。
 */
esp_err_t record_pipe_close(record_pipe_handle_t rp);

/**
 * 获取一帧 Opus 数据（非阻塞）。
 *
 * 无数据时 *size = 0，调用方不应调用 record_pipe_release_frame()。
 *
 * @param rp    句柄
 * @param data  输出：帧数据指针（由 pipeline 管理，不可释放）
 * @param size  输出：帧大小（字节），0 表示暂无数据
 * @return ESP_OK 成功（含暂无数据情况）
 */
esp_err_t record_pipe_acquire_frame(record_pipe_handle_t rp,
                                    const uint8_t **data, int *size);

/**
 * 释放之前通过 record_pipe_acquire_frame() 获取的帧。
 * 必须与 acquire_frame（*size > 0）配对调用。
 */
esp_err_t record_pipe_release_frame(record_pipe_handle_t rp);

#ifdef __cplusplus
}
#endif
