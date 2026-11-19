/**
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wav_codec.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "simple_file_decoder.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_audio_simple_dec.h"
#include "wav_codec.h"
#include "player.h"
#include "bsp_audio.h"
#include "simple_http_decoder.h"
#include "http_stream.h"
#include "wav_codec.h"
#include "ringbuf.h"

#define TAG "SIMP_DEC_TEST"
#define READ_SIZE     (512)

typedef union {
    esp_m4a_dec_cfg_t m4a_cfg;
    esp_ts_dec_cfg_t  ts_cfg;
    esp_aac_dec_cfg_t aac_cfg;
} simp_dec_all_t;

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

void simple_http_read_task(void *arg)
{
    // ESP_LOGE(TAG, "simple_http_read_task begin, RAM left: %ld", esp_get_free_heap_size());
    player_handle_t *player = arg;
    size_t ringBufFreeBytes;
    uint8_t *buffer = NULL;
    size_t len;
    EventBits_t uxBits;
    bool decoder_started = false;

    do {
        if (http_open(player->file_uri) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP stream");
            break;
        }
        buffer = malloc(READ_SIZE);
        if (!buffer) {
            break;
        }

        while (1) { // 填满 ring buffer
            ringBufFreeBytes = rb_bytes_available(player->decode_ringbuff);
            if (ringBufFreeBytes >= READ_SIZE) {
                len = http_read((char *)buffer, READ_SIZE);
                if (len > 0) {
                    rb_write(player->decode_ringbuff, (char *)buffer, len, 100/portTICK_PERIOD_MS);
                }
                if (len < READ_SIZE) {
                    break;
                }
            } else {
                break;
            }
        }

        BaseType_t task_result = xTaskCreatePinnedToCore(
            &simple_http_decoder_task, "simple_http_decoder_task", 4 * 1024,
            (void *)player, 8, &player->simple_http_decoder_task_handle, CORE_NUM1);
        if (task_result != pdPASS) {
            player->simple_http_decoder_task_handle = NULL;
            break;
        }
        decoder_started = true;

        while (1) {
            ringBufFreeBytes = rb_bytes_available(player->decode_ringbuff);
            if (ringBufFreeBytes >= READ_SIZE) {
                len = http_read((char *)buffer, READ_SIZE);
                if (len > 0) {
                    rb_write(player->decode_ringbuff, (char *)buffer, len, 100/portTICK_PERIOD_MS);
                }

                if (len < READ_SIZE) { // 文件读完了
                    break;
                }
            }

            uxBits = xEventGroupWaitBits(
                    player->player_event,    // The event group being tested.
                    EV_PLAY_STOP,  // The bits within the event group to wait for.
                    pdTRUE,         // BIT_0 and BIT_4 should be cleared before returning.
                    pdFALSE,         // not wait for both bits, either bit will do.
                    20 / portTICK_PERIOD_MS ); // Wait a maximum of 100ms for either bit to be set.
            if ((uxBits & EV_PLAY_STOP) == EV_PLAY_STOP) {
                xEventGroupSetBits(player->player_event, EV_READ_END);
                break;
            }
        }
    } while (0);

    if (buffer) {
        free(buffer);
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
    player->simple_http_read_task_handle = NULL;
    if (decoder_started) {
        player_task_done(player, EV_PLAY_HTTP_READ_DONE);
    } else {
        player->simple_http_decoder_task_handle = NULL;
        player_task_done(player,
            EV_PLAY_HTTP_READ_DONE | EV_PLAY_HTTP_DECODER_DONE);
    }
    vTaskDelete(NULL);
}

void simple_http_decoder_task(void *arg)
{
    // ESP_LOGE(TAG, "simple_http_decoder_task begin, RAM left: %ld", esp_get_free_heap_size());
    player_handle_t *player = arg;
    int ret = 0;
    int max_out_size = 4096;
    size_t len;
    uint8_t *in_buf = NULL;
    uint8_t *out_buf = NULL;
    esp_audio_simple_dec_handle_t decoder = NULL;
    EventBits_t uxBits;
    bool decoder_registered = false;
    bool simple_decoder_registered = false;
    bool codec_open = false;
    bool decoder_failed = false;

    do { //执行一次，便于用break实现出错统一退出
        esp_audio_dec_register_default();
        decoder_registered = true;
        if(esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK){
            decoder_failed = true;
            break;
        }
        simple_decoder_registered = true;

        in_buf = malloc(READ_SIZE);
        out_buf = malloc(max_out_size);
        if (in_buf == NULL || out_buf == NULL) {
            ESP_LOGE(TAG, "No memory for decoder");
            ret = ESP_AUDIO_ERR_MEM_LACK;
            decoder_failed = true;
            break;
        }
        
        simp_dec_all_t all_cfg = {};
        http_stream_t *http = get_http_handle();
        if (!http) {
            decoder_failed = true;
            break;
        }
        // ESP_LOGE(TAG, "http stream type : %d", http->stream_type);
        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = http->stream_type,
            .dec_cfg = &all_cfg,
        };
        get_simple_decoder_config(&dec_cfg);
        if (esp_audio_simple_dec_open(&dec_cfg, &decoder) != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Fail to open simple decoder");
            decoder_failed = true;
            break;
        }

        int total_decoded = 0;
        esp_audio_simple_dec_raw_t raw = {
            .buffer = in_buf,
        };
       
        while (1) { //读数据->解码循环
            len = rb_read(player->decode_ringbuff, (char *)in_buf, READ_SIZE, 100/portTICK_PERIOD_MS);
            raw.buffer = in_buf;
            raw.len = len;
            raw.eos = (len < READ_SIZE);
            esp_audio_simple_dec_out_t out_frame = {
                .buffer = out_buf,
                .len = max_out_size,
            };
            // ATTENTION: when input raw data unconsumed (`raw.len > 0`) do not overwrite its content
            // Or-else unexpected error may happen for data corrupt.
            while (raw.len) { //对每次读出的数据解码
                ret = esp_audio_simple_dec_process(decoder, &raw, &out_frame); //解码
                if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    // Handle output buffer not enough case
                    uint8_t *new_buf = realloc(out_buf, out_frame.needed_size);
                    if (new_buf == NULL) {
                        decoder_failed = true;
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
                    decoder_failed = true;
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

            if (decoder_failed) {
                break;
            }
            
            if(raw.eos){ break; } //播完

            uxBits = xEventGroupWaitBits(
                    player->player_event,    // The event group being tested.
                    EV_READ_END,  // The bits within the event group to wait for.
                    pdTRUE,         // BIT_0 and BIT_4 should be cleared before returning.
                    pdFALSE,         // not wait for both bits, either bit will do.
                    5 / portTICK_PERIOD_MS ); // Wait a maximum of 100ms for either bit to be set.
            if( ( uxBits & EV_READ_END) ==  EV_READ_END ){
                break;
            }  
        }
    } while (0);

    if (decoder_failed) {
        xEventGroupSetBits(player->player_event, EV_PLAY_STOP);
    }
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
    if (in_buf) {
        free(in_buf);
    }
    if (out_buf) {
        free(out_buf);
    }
    player->simple_http_decoder_task_handle = NULL;
    player_task_done(player, EV_PLAY_HTTP_DECODER_DONE);
    // ESP_LOGE(TAG, "simple_http_decoder_task end, RAM left: %ld", esp_get_free_heap_size());
    vTaskDelete(NULL);
}
