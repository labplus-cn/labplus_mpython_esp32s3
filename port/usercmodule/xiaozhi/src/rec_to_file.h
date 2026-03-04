/*
 * rec_to_file.h - 录音保存为标准 OGG/Opus 文件
 *
 * 文件格式：OGG 容器（RFC 3533）+ Opus 音频（RFC 7845）
 *   可被 VLC、ffplay、mpv 等播放器直接打开。
 *
 * 前置条件：audio.init() 已调用（音频硬件已初始化）。
 * 不能与 xiaozhi 会话同时运行（共用同一麦克风硬件）。
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 将麦克风录音保存为 OGG/Opus 文件（同步阻塞）。
 *
 * @param path             目标文件路径（LittleFS，如 "/rec.opus"）
 * @param duration_ms      录音时长（毫秒）
 * @param use_afe          true：AFE pipeline（ai_afe → aud_enc，启用唤醒词检测）
 *                         false：回退 pipeline（aud_ch_cvt → aud_enc）
 * @param out_wakeup_count 输出：录音期间检测到唤醒词的次数（可为 NULL）。
 *                         use_afe=false 时始终写 0。
 * @return ESP_OK 成功；ESP_ERR_INVALID_STATE 硬件未初始化；其他错误见 esp_err.h
 */
esp_err_t rec_to_file(const char *path, uint32_t duration_ms,
                      bool use_afe, int *out_wakeup_count);

#ifdef __cplusplus
}
#endif
