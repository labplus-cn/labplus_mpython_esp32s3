/**
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "simple_file_decoder.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_audio_simple_dec.h"
#include "esp_timer.h"
#include "vfs_lfs2.h"
#include "wav_codec.h"
#include "player.h"
#include "bsp_audio.h"

#define TAG "SIMP_DEC_TEST"

typedef union {
    esp_m4a_dec_cfg_t m4a_cfg;
    esp_ts_dec_cfg_t  ts_cfg;
    esp_aac_dec_cfg_t aac_cfg;
} simp_dec_all_t;

static esp_audio_simple_dec_type_t get_simple_decoder_type(const char *file)
{
    char *ext = strrchr(file, '.');
    if (ext == NULL) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    }
    ext++;
    if (strcasecmp(ext, "aac") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    }
    if (strcasecmp(ext, "mp3") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }
    if (strcasecmp(ext, "amrnb") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AMRNB;
    }
    if (strcasecmp(ext, "amrwb") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AMRWB;
    }
    if (strcasecmp(ext, "flac") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    }
    if (strcasecmp(ext, "wav") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    }
    if (strcasecmp(ext, "mp4") == 0 || strcasecmp(ext, "m4a") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    }
    if (strcasecmp(ext, "ts") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_TS;
    }
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

static void get_simple_decoder_config(esp_audio_simple_dec_cfg_t *cfg)
{
    simp_dec_all_t *all_cfg = (simp_dec_all_t *)&cfg->dec_cfg;
    switch (cfg->dec_type) {
        case ESP_AUDIO_SIMPLE_DEC_TYPE_AAC: {
            esp_aac_dec_cfg_t *aac_cfg = &all_cfg->aac_cfg;
            aac_cfg->aac_plus_enable = true;
            cfg->cfg_size = sizeof(esp_aac_dec_cfg_t);
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_M4A: {
            esp_m4a_dec_cfg_t *m4a_cfg = &all_cfg->m4a_cfg;
            m4a_cfg->aac_plus_enable = true;
            cfg->cfg_size = sizeof(esp_m4a_dec_cfg_t);
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_TS: {
            esp_ts_dec_cfg_t *ts_cfg = &all_cfg->ts_cfg;
            ts_cfg->aac_plus_enable = true;
            cfg->cfg_size = sizeof(esp_ts_dec_cfg_t);
            break;
        }
        default:
            break;
    }
}

void simple_file_decoder_task(void *arg)
{
    player_handle_t *player = arg;
    int ret = 0;
    int max_out_size = 4096;
    int read_size = 512;
    size_t len;
    uint8_t *in_buf = NULL;
    uint8_t *out_buf = NULL;
    esp_audio_simple_dec_handle_t decoder = NULL;
    wav_codec_t *wav_codec = NULL;
    bool file_open = false;
    bool decoder_registered = false;
    bool simple_decoder_registered = false;
    bool codec_open = false;
    bool decode_failed = false;
    bool stop_requested = false;

    esp_audio_simple_dec_type_t type = get_simple_decoder_type(player->file_uri);
    if (type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
        ESP_LOGE(TAG, "Not supported file format %s", player->file_uri);
        goto cleanup;
    }

	wav_codec = (wav_codec_t*) calloc(1, sizeof(wav_codec_t));
	if (!wav_codec) {
		ESP_LOGE(TAG, "wav_codec is null.");
		goto cleanup;
	}

	wav_codec->lfs2_file = vfs_lfs2_file_open(player->file_uri, LFS2_O_WRONLY);
	if (!wav_codec->lfs2_file) {
		ESP_LOGE(TAG, "file open fault.");
		goto cleanup;
	}
    file_open = true;

    do { //执行一次，便于用break实现出错统一退出
        esp_audio_dec_register_default();
        decoder_registered = true;
        if(esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK){
            break;
        }
        simple_decoder_registered = true;

        in_buf = malloc(read_size);
        out_buf = malloc(max_out_size);
        if (in_buf == NULL || out_buf == NULL) {
            ESP_LOGI(TAG, "No memory for decoder");
            ret = ESP_AUDIO_ERR_MEM_LACK;
            break;
        }
        
        simp_dec_all_t all_cfg = {};
        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = type,
            .dec_cfg = &all_cfg,
        };
        get_simple_decoder_config(&dec_cfg);
        if (esp_audio_simple_dec_open(&dec_cfg, &decoder) != ESP_AUDIO_ERR_OK) {
            ESP_LOGI(TAG, "Fail to open simple decoder");
            break;
        }

        int total_decoded = 0;
        uint64_t decode_time = 0;
        esp_audio_simple_dec_raw_t raw = {
            .buffer = in_buf,
        };
        uint64_t read_start = esp_timer_get_time();
        while (1) { //读数据->解码循环
            EventBits_t stop_bits = xEventGroupWaitBits(
                player->player_event, EV_PLAY_STOP, pdTRUE, pdFALSE, 0);
            if ((stop_bits & EV_PLAY_STOP) != 0) {
                stop_requested = true;
                break;
            }

            len = lfs2_file_read(&wav_codec->lfs2_file->vfs->lfs, &wav_codec->lfs2_file->file, in_buf, read_size);
            raw.buffer = in_buf;
            raw.len = len;
            raw.eos = (len < read_size);
            esp_audio_simple_dec_out_t out_frame = {
                .buffer = out_buf,
                .len = max_out_size,
            };
            // ATTENTION: when input raw data unconsumed (`raw.len > 0`) do not overwrite its content
            // Or-else unexpected error may happen for data corrupt.
            while (raw.len) { //对每次读出的数据解码
                stop_bits = xEventGroupWaitBits(
                    player->player_event, EV_PLAY_STOP, pdTRUE, pdFALSE, 0);
                if ((stop_bits & EV_PLAY_STOP) != 0) {
                    stop_requested = true;
                    break;
                }

                uint64_t start = esp_timer_get_time();  //
                if (start > read_start + 30000000) {
                    raw.eos = true;
                    break;
                }
                ret = esp_audio_simple_dec_process(decoder, &raw, &out_frame); //解码
                decode_time += esp_timer_get_time() - start;
                if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    // Handle output buffer not enough case
                    uint8_t *new_buf = realloc(out_buf, out_frame.needed_size);
                    if (new_buf == NULL) {
                        decode_failed = true;
                        break;
                    }
                    out_buf = new_buf;
                    out_frame.buffer = new_buf;
                    max_out_size = out_frame.needed_size;
                    out_frame.len = max_out_size;
                    continue;
                }
                if (ret != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(TAG, "Fail to decode data ret %d", ret);
                    decode_failed = true;
                    break;
                }
                if (out_frame.decoded_size) { //有解码数据
                    if (total_decoded == 0) { //读的文件头部数据，包含音频信息，提取音频信息。
                        // Update audio information
                        esp_audio_simple_dec_info_t dec_info = {};
                        esp_audio_simple_dec_get_info(decoder, &dec_info);
                        bsp_codec_dev_open(dec_info.sample_rate, dec_info.channel, dec_info.bits_per_sample);
                        codec_open = true;
                    }
                    total_decoded += out_frame.decoded_size;
                    bsp_audio_play((int8_t *)out_frame.buffer, out_frame.decoded_size, portMAX_DELAY);
                }
                // In case that input data contain multiple frames
                raw.len -= raw.consumed;
                raw.buffer += raw.consumed;
            }

            if (decode_failed || stop_requested) {
                break;
            }
            
            if (raw.eos) { //文件末，结束读数据->解码循环，结束播放
                break;
            }
        }

    } while (0);

cleanup:
    if (codec_open) {
        bsp_codec_dev_close();
    }
    if (decoder) {
        esp_audio_simple_dec_close(decoder);
    }
    if (simple_decoder_registered) {
        esp_audio_simple_dec_unregister_default();
    }
    if (decoder_registered) {
        esp_audio_dec_unregister_default();
    }
    if (file_open) {
        vfs_lfs2_file_close(wav_codec->lfs2_file);
    }
    if (wav_codec) {
        free(wav_codec);
    }
    if (in_buf) {
        free(in_buf);
    }
    if (out_buf) {
        free(out_buf);
    }
    player->simple_file_decoder_task_handle = NULL;
    player_task_done(player, EV_PLAY_FILE_DONE);
    vTaskDelete(NULL);
}
