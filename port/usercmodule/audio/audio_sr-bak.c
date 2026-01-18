/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "py/objstr.h"
#include "py/runtime.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "filter_resample.h"
#include "raw_stream.h"
#include "i2s_stream.h"

#include "board.h"
#include "modaudio.h"
// #include "mpconfigboard.h"
// #include <sys/_intsup.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/event_groups.h"
// #include "freertos/queue.h"
// #include "freertos/task.h"

#include "amrnb_encoder.h"
#include "amrwb_encoder.h"
#include "audio_element.h"
#include "audio_idf_version.h"
#include "audio_recorder.h"
#include "audio_mem.h"
#include "audio_thread.h"
#include "esp_audio.h"
#include "recorder_sr.h"
// #include "recorder_encoder.h"
// #include "recorder_sr.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

#define TAG "SR_MODULE"

#define NO_ENCODER  (0)
#define ENC_2_AMRNB (1)
#define ENC_2_AMRWB (2)

#define RECORDER_ENC_ENABLE (NO_ENCODER)
#define VOICE2FILE          (false)
#define WAKENET_ENABLE      (true)
#define MULTINET_ENABLE     (true)
#define SPEECH_CMDS_RESET   (true)

#define SPEECH_COMMANDS     ("da kai dian deng,kai dian deng;guan bi dian deng,guan dian deng;guan deng;")
#define RECORD_HARDWARE_AEC  (false)

enum _rec_msg_id {
    REC_START = 1,
    REC_STOP,
    REC_CANCEL,
};

volatile int latest_command_id = 0;
// static esp_audio_handle_t     player        = NULL;
static audio_rec_handle_t     recorder      = NULL;
static audio_element_handle_t raw_read      = NULL;
static QueueHandle_t          rec_q         = NULL;
static bool                   voice_reading = false;
static char                   wakeup_rsp[64] = {0};
static void voice_read_task(void *args)
{
    const int buf_len = 2 * 1024;
    uint8_t *voiceData = audio_calloc(1, buf_len);
    int msg = 0;
    TickType_t delay = portMAX_DELAY;

    while (true) {
        if (xQueueReceive(rec_q, &msg, delay) == pdTRUE) {
            switch (msg) {
                case REC_START: {
                    ESP_LOGW(TAG, "voice read begin");
                    delay = 0;
                    voice_reading = true;
                    break;
                }
                case REC_STOP: {
                    ESP_LOGW(TAG, "voice read stopped");
                    delay = portMAX_DELAY;
                    voice_reading = false;
                    break;
                }
                case REC_CANCEL: {
                    ESP_LOGW(TAG, "voice read cancel");
                    delay = portMAX_DELAY;
                    voice_reading = false;
                    break;
                }
                default:
                    break;
            }
        }
        int ret = 0;
        if (voice_reading) {
            ret = audio_recorder_data_read(recorder, voiceData, buf_len, portMAX_DELAY);
            if (ret <= 0) {
                ESP_LOGW(TAG, "audio recorder read finished %d", ret);
                delay = portMAX_DELAY;
                voice_reading = false;
            }
        }
    }

    free(voiceData);
    vTaskDelete(NULL);
}

static esp_err_t rec_engine_cb(audio_rec_evt_t *event, void *user_data)
{
    // ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_WAKEUP_START");
  
    if (AUDIO_REC_WAKEUP_START == event->type) {
        recorder_sr_wakeup_result_t *wakeup_result = event->event_data;
        // tts_generate(wakeup_rsp);
        ESP_LOGI(TAG, "wakeup: vol %f, mod idx %d, word idx %d", wakeup_result->data_volume, wakeup_result->wakenet_model_index, wakeup_result->wake_word_index);
        if (voice_reading) {
            int msg = REC_CANCEL;
            if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "rec cancel send failed");
            }
        }
    } else if (AUDIO_REC_VAD_START == event->type) {
        ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_VAD_START");
        if (!voice_reading) {
            int msg = REC_START;
            if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "rec start send failed");
            }
        }
    } else if (AUDIO_REC_VAD_END == event->type) {
        ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_VAD_STOP");
        if (voice_reading) {
            int msg = REC_STOP;
            if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "rec stop send failed");
            }
        }

    } else if (AUDIO_REC_WAKEUP_END == event->type) {
        ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_WAKEUP_END");
        AUDIO_MEM_SHOW(TAG);
    } else if (AUDIO_REC_COMMAND_DECT <= event->type) {
        recorder_sr_mn_result_t *mn_result = event->event_data;
        latest_command_id = mn_result->phrase_id;
        tts_generate(wakeup_rsp);
        ESP_LOGI(TAG, "rec_engine_cb - AUDIO_REC_COMMAND_DECT");
        ESP_LOGW(TAG, "command %d, phrase_id %d, prob %f, str: %s"
            , event->type, mn_result->phrase_id, mn_result->prob, mn_result->str);
    } else {
        ESP_LOGE(TAG, "Unkown event");
    }
    return ESP_OK;
}

static int input_cb_for_afe(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    return raw_stream_read(raw_read, (char *)buffer, buf_sz);
}

static void start_recorder()
{
    char *audio_sr_input_fmt = AUDIO_ADC_INPUT_CH_FORMAT;
    audio_element_handle_t i2s_stream_reader;
    audio_pipeline_handle_t pipeline;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (NULL == pipeline) {
        return;
    }

    int bits_per_sample = CODEC_ADC_BITS_PER_SAMPLE;
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(CODEC_ADC_I2S_PORT, 16000, bits_per_sample, AUDIO_STREAM_READER);
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    audio_element_handle_t filter = NULL;
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 16000;
    rsp_cfg.dest_rate = 16000;
    rsp_cfg.dest_ch = 1;
    filter = rsp_filter_init(&rsp_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_read = raw_stream_init(&raw_cfg);

    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline, raw_read, "raw");

    if (filter) {
        audio_pipeline_register(pipeline, filter, "filter");
        const char *link_tag[3] = {"i2s", "filter", "raw"};
        audio_pipeline_link(pipeline, &link_tag[0], 3);
    } else {
        const char *link_tag[2] = {"i2s", "raw"};
        audio_pipeline_link(pipeline, &link_tag[0], 2);
    }

    audio_pipeline_run(pipeline);

    recorder_sr_cfg_t recorder_sr_cfg = DEFAULT_RECORDER_SR_CFG(audio_sr_input_fmt, "sr_module", AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    recorder_sr_cfg.afe_cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    recorder_sr_cfg.afe_cfg->wakenet_init = WAKENET_ENABLE;
    // recorder_sr_cfg.afe_cfg->vad_mode = VAD_MODE_4;
    recorder_sr_cfg.multinet_init = MULTINET_ENABLE;
#if !defined(CONFIG_SR_MN_CN_NONE)
    recorder_sr_cfg.mn_language = ESP_MN_CHINESE;
#elif !defined(CONFIG_SR_MN_EN_NONE)
    recorder_sr_cfg.mn_language = ESP_MN_ENGLISH;
#else
    recorder_sr_cfg.mn_language = "";
#endif
    recorder_sr_cfg.afe_cfg->aec_init = RECORD_HARDWARE_AEC;
    recorder_sr_cfg.afe_cfg->agc_mode = AFE_MN_PEAK_NO_AGC;

    audio_rec_cfg_t cfg = AUDIO_RECORDER_DEFAULT_CFG();
    cfg.read = (recorder_data_read_t)&input_cb_for_afe;
    cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &cfg.sr_iface);
#if SPEECH_CMDS_RESET
    char err[200];
    recorder_sr_reset_speech_cmd(cfg.sr_handle, SPEECH_COMMANDS, err);
#endif
    // esp_mn_commands_clear();
    // esp_mn_commands_update();

    cfg.event_cb = rec_engine_cb;
    cfg.vad_off = 1000;
    recorder = audio_recorder_create(&cfg);
}

static void log_clear(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_log_level_set("AUDIO_THREAD", ESP_LOG_ERROR);
    esp_log_level_set("I2C_BUS", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_HAL", ESP_LOG_ERROR);
    esp_log_level_set("ESP_AUDIO_TASK", ESP_LOG_ERROR);
    esp_log_level_set("ESP_DECODER", ESP_LOG_ERROR);
    esp_log_level_set("I2S", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_FORGE", ESP_LOG_ERROR);
    esp_log_level_set("ESP_AUDIO_CTRL", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_ERROR);
    esp_log_level_set("TONE_PARTITION", ESP_LOG_ERROR);
    esp_log_level_set("TONE_STREAM", ESP_LOG_ERROR);
    esp_log_level_set("MP3_DECODER", ESP_LOG_ERROR);
    esp_log_level_set("I2S_STREAM", ESP_LOG_ERROR);
    esp_log_level_set("RSP_FILTER", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_EVT", ESP_LOG_ERROR);
}

static mp_obj_t mp_sr_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_wakeup, ARG_timeout};
    static const mp_arg_t allowed_args[] = {
    { MP_QSTR_wakeup_word, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },  // 唤醒词
    { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 6000} },              // 超时时间
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *word = mp_obj_str_get_str(args[0].u_obj);
    size_t len = strlen(word);
    len = (len > 63) ? 63 : len;
    memcpy(wakeup_rsp, word, len);
    wakeup_rsp[len] = '\0';
    // uint16_t t = args[1].u_int;

    log_clear();
    // audio_board_init();
    start_recorder();

    rec_q = xQueueCreate(3, sizeof(int));
    audio_thread_create(NULL, "read_task", voice_read_task, NULL, 4 * 1024, 5, true, 0);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_sr_init_obj, 0, mp_sr_init);

// 清空语音指令
static mp_obj_t mp_esp_mn_commands_clear(void) {
    esp_mn_commands_clear();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_esp_mn_commands_clear_obj, mp_esp_mn_commands_clear);

// 添加语音指令，参数：command_id（整数,不可为0），text（拼音字符串）
static mp_obj_t mp_esp_mn_commands_add(mp_obj_t command_id_obj, mp_obj_t text_obj) {
    int command_id = mp_obj_get_int(command_id_obj);
    const char *text = mp_obj_str_get_str(text_obj);
    esp_mn_commands_add(command_id, text);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_esp_mn_commands_add_obj, mp_esp_mn_commands_add);

// 应用更新
static mp_obj_t mp_esp_mn_commands_update(void) {
    esp_mn_commands_update();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_esp_mn_commands_update_obj, mp_esp_mn_commands_update);

static mp_obj_t mp_sc_get_latest_command_id(void) {
    int cmd_id = latest_command_id;
    latest_command_id = 0;
    return mp_obj_new_int(cmd_id);
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_sc_get_latest_command_id_obj, mp_sc_get_latest_command_id);