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

#include <stdio.h>
#include <string.h>
#include "py/objstr.h"
#include "py/runtime.h"
#include "esp_log.h"
#include "esp_err.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_alc.h"

#include "esp_audio_simple_player.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_sys.h"
#include "esp_gmf_io.h"

#include "modaudio.h"

static const char *TAG = "AUDIO_MOD";

esp_asp_handle_t  player_handle = NULL;

static int out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    esp_codec_dev_handle_t dev = (esp_codec_dev_handle_t)ctx;
    esp_codec_dev_write(dev, data, data_size);
    return 0;
}

static int in_data_callback(uint8_t *data, int data_size, void *ctx)
{
    int ret = fread(data, 1, data_size, ctx);
    ESP_LOGD(TAG, "%s-%d,rd size:%d", __func__, __LINE__, ret);
    return ret;
}

static int mock_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get info, rate:%d, channels:%d, bits:%d", info.sample_rate, info.channels, info.bits);
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get State, %d,%s", st, esp_audio_simple_player_state_to_str(st));
        if (ctx && ((st == ESP_ASP_STATE_STOPPED) || (st == ESP_ASP_STATE_FINISHED) || (st == ESP_ASP_STATE_ERROR))) {
            xSemaphoreGive((SemaphoreHandle_t)ctx);
        }
    }
    return 0;
}

static mp_obj_t audio_init(void)
{
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 16000;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);

    // Set default output volume range from [0, 100]
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();
    esp_codec_dev_set_out_vol(codec_dev , 70);
    esp_codec_dev_set_in_gain(codec_dev, 35.0);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_init_obj, audio_init);

static mp_obj_t audio_deinit(void)
{
    esp_gmf_app_teardown_codec_dev();

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_deinit_obj, audio_deinit);

static mp_obj_t audio_player_init(size_t n_args, const mp_obj_t *args)
{
    esp_asp_cfg_t cfg = {
        // .in.cb = in_cb,
        // .in.user_ctx = in_ctx,
        .out.cb = out_data_callback,
        .out.user_ctx = esp_gmf_app_get_playback_handle(),
        .task_prio = 5,
    };

    if(player_handle == NULL){
        esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &player_handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        err = esp_audio_simple_player_set_event(player_handle, mock_event_callback, NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(audio_player_init_obj, 0, 1, audio_player_init);

static mp_obj_t audio_player_deinit(void)
{
    esp_gmf_err_t err = esp_audio_simple_player_stop(player_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_destroy(player_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_player_deinit_obj, audio_player_deinit);

static mp_obj_t audio_play(mp_obj_t uri)
{
    const char *_uri = mp_obj_str_get_str(uri);
    esp_gmf_err_t err = esp_audio_simple_player_run(player_handle, _uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_play_obj, audio_play);

static mp_obj_t audio_pause(void)
{
    assert(player_handle != NULL);
    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
    err = esp_audio_simple_player_pause(player_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    return  mp_const_none;   
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_pause_obj, audio_pause);

static mp_obj_t audio_resume(void)
{
    assert(player_handle != NULL);
    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_PAUSED, state);
    err = esp_audio_simple_player_resume(player_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);  

    return mp_const_none;  
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_resume_obj, audio_resume);

static mp_obj_t audio_stop(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    assert(player_handle != NULL);
    esp_gmf_err_t err = esp_audio_simple_player_stop(player_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    return mp_const_none;   
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_stop_obj, audio_stop);

static mp_obj_t audio_volume(mp_obj_t Volume)
{
    mp_int_t vol =  mp_obj_get_int(Volume);
    if(vol > 100){ vol = 100;}
    if(vol < 0){ vol = 0;}
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();
    esp_codec_dev_set_out_vol(codec_dev, vol);

    return mp_const_none;   
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_volume_obj, audio_volume);

static mp_obj_t audio_get_status(void)
{
    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);  

    return MP_OBJ_NEW_SMALL_INT(state);
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_get_status_obj, audio_get_status);

extern const mp_obj_type_t audio_player_type;
extern const mp_obj_type_t audio_recorder_type;

static const mp_rom_map_elem_t audio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_audio) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&audio_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___deinit__), MP_ROM_PTR(&audio_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_player_init), MP_ROM_PTR(&audio_player_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_player_deinit), MP_ROM_PTR(&audio_player_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_play), MP_ROM_PTR(&audio_play_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_resume), MP_ROM_PTR(&audio_resume_obj)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_stop), MP_ROM_PTR(&audio_stop_obj)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_pause), MP_ROM_PTR(&audio_pause_obj)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_volume),  MP_ROM_PTR(&audio_volume_obj)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_player_status),  MP_ROM_PTR(&audio_get_status_obj)},

    { MP_ROM_QSTR(MP_QSTR_tts), MP_ROM_PTR(&tts_module) },

};

static MP_DEFINE_CONST_DICT(audio_module_globals, audio_module_globals_table);

const mp_obj_module_t audio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&audio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_audio, audio_module);
