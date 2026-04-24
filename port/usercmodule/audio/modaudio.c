#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "py/objstr.h"
#include "py/runtime.h"
#include "esp_log.h"
#include "esp_err.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_task.h"
#include "esp_gmf_info.h"
#include "esp_gmf_alc.h"

#include "esp_audio_simple_player.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_sys.h"
#include "esp_gmf_io.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_audio_types.h"

#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_capture_audio_dev_src.h"
#include "esp_capture_advance.h"
#include "esp_gmf_audio_enc.h"
#include "esp_opus_enc.h"
#include "esp_audio_enc.h"
#include "esp_littlefs.h"
#include "driver/gpio.h"

#include "modaudio.h"
#include "format_writer.h"

static const char *TAG = "AUDIO_MOD";
static bool lfs_isRegist = false;
static int8_t *stream_buff = NULL;
#define READ_BUFF_SIZE    (1000)

/* Detect format from filename extension */
static const char *detect_format_from_filename(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return "wav";  // Default to WAV

    ext++;  // Skip the '.'
    if (strcasecmp(ext, "wav") == 0) return "wav";
    if (strcasecmp(ext, "opus") == 0) return "opus";

    return "wav";  // Default
}

/* ------------------- 播放器 ------------------- */
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

    if(!lfs_isRegist){
        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/littlefs",
            .partition_label = "vfs",
            .format_if_mount_failed = false,
            .dont_mount = false,
        };

        esp_err_t ret = esp_vfs_littlefs_register(&conf);
        if (ret != ESP_OK) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to mount LittleFS"));
        }

        lfs_isRegist = true;
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_init_obj, audio_init);

static mp_obj_t audio_deinit(void)
{
    esp_vfs_littlefs_unregister("littlefs");
    esp_gmf_app_teardown_codec_dev();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_deinit_obj, audio_deinit);

static mp_obj_t audio_player_init(size_t n_args, const mp_obj_t *args)
{
    esp_asp_cfg_t cfg = {
        .out.cb = out_data_callback,
        .out.user_ctx = esp_gmf_app_get_playback_handle(),
        .task_prio = 5,
    };

    if(player_handle == NULL){
        esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &player_handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        err = esp_audio_simple_player_set_event(player_handle, mock_event_callback, NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        esp_codec_dev_handle_t playback_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();
        esp_codec_dev_set_out_vol(playback_dev, 80);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(audio_player_init_obj, 0, 1, audio_player_init);

static mp_obj_t audio_player_deinit(void)
{
    if (player_handle == NULL) {
        return mp_const_none;
    }

    // Get player state
    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle, &state);
    if (err != ESP_OK) {
        player_handle = NULL;
        return mp_const_none;
    }

    // Only destroy in safe states (STOPPED, FINISHED, ERROR, NONE)
    // Don't destroy if RUNNING or PAUSED
    if (state == ESP_ASP_STATE_RUNNING || state == ESP_ASP_STATE_PAUSED) {
        return mp_const_none;
    }

    // Safe to destroy
    esp_audio_simple_player_destroy(player_handle);
    player_handle = NULL;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_player_deinit_obj, audio_player_deinit);

static mp_obj_t audio_play(mp_obj_t uri)
{
    if (player_handle == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Player not initialized"));
    }

    const char *_uri = mp_obj_str_get_str(uri);
    
    // If HTTP URL, pass directly to player
    if (strncmp(_uri, "http", 4) == 0) {
        esp_gmf_err_t err = esp_audio_simple_player_run(player_handle, _uri, NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        return mp_const_none;
    }

    // Allocate buffers dynamically
    char *file_path = malloc(256);
    char *full_uri = malloc(272);
    if (!file_path || !full_uri) {
        free(file_path);
        free(full_uri);
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Out of memory"));
    }

    // Prepend /littlefs/ to uri
    snprintf(file_path, 256, "/littlefs/%s", _uri);

    // Test if file can be opened
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        free(file_path);
        free(full_uri);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("File not found"));
    }
    fclose(f);

    // Prepend file:/ for player API
    snprintf(full_uri, 272, "file:/%s", file_path);

    esp_gmf_err_t err = esp_audio_simple_player_run(player_handle, full_uri, NULL);

    free(file_path);
    free(full_uri);

    TEST_ASSERT_EQUAL(ESP_OK, err);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_play_obj, audio_play);

static mp_obj_t audio_pause(void)
{
    if (player_handle == NULL) {
        return mp_const_none;
    }

    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Only pause if currently running
    if (state != ESP_ASP_STATE_RUNNING) {
        return mp_const_none;
    }

    err = esp_audio_simple_player_pause(player_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    return  mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_pause_obj, audio_pause);

static mp_obj_t audio_resume(void)
{
    if (player_handle == NULL) {
        return mp_const_none;
    }

    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Only resume if currently paused
    if (state != ESP_ASP_STATE_PAUSED) {
        return mp_const_none;
    }

    err = esp_audio_simple_player_resume(player_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_resume_obj, audio_resume);

static mp_obj_t audio_stop(void)
{
    if (player_handle == NULL) {
        return mp_const_none;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
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
    if (player_handle == NULL) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }

    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    return MP_OBJ_NEW_SMALL_INT(state);
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_get_status_obj, audio_get_status);

/* ------------------- 录音 ------------------- */
static mp_obj_t audio_recorder_init(size_t n_args, const mp_obj_t *args)
{
    esp_codec_dev_set_in_gain((esp_codec_dev_handle_t)esp_gmf_app_get_record_handle(), 35.0);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(audio_recorder_init_obj, 0, 1, audio_recorder_init);

static mp_obj_t audio_recorder_deinit(void)
{
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_recorder_deinit_obj, audio_recorder_deinit);

static mp_obj_t audio_loudness(void)
{
    double loudness = 0.0;

    if(!stream_buff){
        stream_buff = calloc(100, sizeof(int8_t));
    }

    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    esp_codec_dev_read(codec_dev, stream_buff, 100);
    
    for(int i = 0; i < 50; i++){
        // loudness += *((int16_t *)stream_buff + i) < 0 ? -*((int16_t *)stream_buff + i) : *((int16_t *)stream_buff + i);
        loudness += (double)(pow(*((int16_t *)stream_buff + i), 2));
    }
    uint16_t g_loudness = (uint16_t)(20 * log10(sqrt(loudness / 50) * 5)); //+ 1e-9)); // 5 is the gain of the microphone,校正倍数

    return MP_OBJ_NEW_SMALL_INT(g_loudness);
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_loudness_obj, audio_loudness);

static mp_obj_t audio_loudness_stop(void)
{
    if(stream_buff){
        free(stream_buff);
        stream_buff = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_loudness_stop_obj, audio_loudness_stop);

/*
 * audio.record(file_name, record_time=5, bits_per_sample=16, channels=1, sampleRate=16000)
 *
 * 录音保存为指定格式文件，格式从文件扩展名自动检测（.wav/.opus）。
 * 使用 esp_capture 组件进行音频采集，通过 format_writer 接口写入文件。
 * 文件操作使用 POSIX 接口（joltwallet__littlefs）。
 *
 * WAV: 直接保存 PCM 数据
 * OPUS: 使用 esp_capture 内置 Opus 编码器
 */
static mp_obj_t audio_record(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_file_name, ARG_record_time, ARG_bits_per_sample, ARG_channels, ARG_sampleRate };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_file_name,       MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_record_time,     MP_ARG_INT, {.u_int = 5} },
        { MP_QSTR_bits_per_sample, MP_ARG_INT, {.u_int = 16} },
        { MP_QSTR_channels,        MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_sampleRate,      MP_ARG_INT, {.u_int = 16000} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *file_name   = mp_obj_str_get_str(args[ARG_file_name].u_obj);
    int record_time         = args[ARG_record_time].u_int;
    int bits_per_sample     = args[ARG_bits_per_sample].u_int;
    int target_ch           = args[ARG_channels].u_int;
    int target_rate         = args[ARG_sampleRate].u_int;

    // Prepend /littlefs/ to file_name
    char actual_path[256];
    snprintf(actual_path, sizeof(actual_path), "/littlefs/%s", file_name);

    // Auto-detect format from filename
    const char *format = detect_format_from_filename(file_name);

    /* Select format writer */
    const format_writer_ops_t *writer_ops = NULL;
    if (strcmp(format, "wav") == 0) {
        writer_ops = get_wav_writer_ops();
    } else if (strcmp(format, "opus") == 0) {
        writer_ops = get_opus_writer_ops();
    }

    /* Open file */
    FILE *f = fopen(actual_path, "wb");
    if (!f) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to open file"));
    }

    // esp_ae_ch_cvt_cfg_t config;
    // float w_data[2] = {1.0, 1.0};
    // config.sample_rate = 16000;
    // config.src_ch = 2;
    // config.dest_ch = 1;
    // config.weight = w_data;
    // config.weight_len = 2;
    // config.bits_per_sample = 16;
    // void *c_handle = NULL;
    // if(target_ch == 1){
    //     int ret = esp_ae_ch_cvt_open(&config, &c_handle);
    //     TEST_ASSERT_NOT_EQUAL(c_handle, NULL);
    // }

    // Setup audio format
    audio_format_t fmt = {
        .sample_rate = target_rate,
        .channels = target_ch,
        .bits_per_sample = bits_per_sample,
    };

    /* Initialize format writer */
    uint8_t writer_buf[256];
    writer_ops->open((format_writer_handle_t)writer_buf, f, &fmt);

    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    
    /* Capture and write frames */
    TickType_t start_time = xTaskGetTickCount();
    TickType_t duration_ticks = pdMS_TO_TICKS(record_time * 1000);

    int8_t *buffer = calloc(READ_BUFF_SIZE, sizeof(int8_t));
	if(!buffer){
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("buffer calloc failt."));
	}
    
    int8_t *pcm_buff = calloc(READ_BUFF_SIZE/2, sizeof(int8_t));
	if(!pcm_buff){
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("pcm_buff calloc failt."));
	}

    while ((xTaskGetTickCount() - start_time) < duration_ticks) {
        if(esp_codec_dev_read(codec_dev, buffer, READ_BUFF_SIZE) == ESP_CODEC_DEV_OK){
            if(target_ch == 1){
                // esp_ae_ch_cvt_process(c_handle, READ_BUFF_SIZE, (void *)buffer, pcm_buff);
                for(int i = 0; i < READ_BUFF_SIZE / 4; i++){
                    *((uint16_t*)pcm_buff + i) = *((uint16_t*)buffer + (i<<1));
                    *((uint16_t*)pcm_buff + i) <<= 3;
                }
                writer_ops->write((format_writer_handle_t)writer_buf, (void *)pcm_buff, READ_BUFF_SIZE/2); 
            }else{
                for(int i = 0; i < READ_BUFF_SIZE / 2; i++){
                    *((uint16_t*)buffer + i) <<= 3;
                }            
                writer_ops->write((format_writer_handle_t)writer_buf, (void *)buffer, READ_BUFF_SIZE);
            }
        }
    }

    /* Finalize format writer */
    writer_ops->close((format_writer_handle_t)writer_buf);
    if(target_ch == 1){
        free(pcm_buff);
        // esp_ae_ch_cvt_close(c_handle);
    }
    free(buffer);
    /* Cleanup */
    fclose(f);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(audio_record_obj, 0, audio_record);

static const mp_rom_map_elem_t audio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_audio) },
    { MP_ROM_QSTR(MP_QSTR___init__),      MP_ROM_PTR(&audio_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___deinit__),    MP_ROM_PTR(&audio_deinit_obj) },

    { MP_ROM_QSTR(MP_QSTR_player_init),   MP_ROM_PTR(&audio_player_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_player_deinit), MP_ROM_PTR(&audio_player_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_play),          MP_ROM_PTR(&audio_play_obj) },
    { MP_ROM_QSTR(MP_QSTR_resume),        MP_ROM_PTR(&audio_resume_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),          MP_ROM_PTR(&audio_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_pause),         MP_ROM_PTR(&audio_pause_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_volume),    MP_ROM_PTR(&audio_volume_obj) },
    { MP_ROM_QSTR(MP_QSTR_player_status), MP_ROM_PTR(&audio_get_status_obj) },

    { MP_ROM_QSTR(MP_QSTR_recorder_init),   MP_ROM_PTR(&audio_recorder_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_recorder_deinit), MP_ROM_PTR(&audio_recorder_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_record),          MP_ROM_PTR(&audio_record_obj) },
    { MP_ROM_QSTR(MP_QSTR_loudness),        MP_ROM_PTR(&audio_loudness_obj) },
    { MP_ROM_QSTR(MP_QSTR_loudness_stop),   MP_ROM_PTR(&audio_loudness_stop_obj) },

    { MP_ROM_QSTR(MP_QSTR_tts), MP_ROM_PTR(&tts_module) },
    { MP_ROM_QSTR(MP_QSTR_sr),  MP_ROM_PTR(&sr_module) },
};

static MP_DEFINE_CONST_DICT(audio_module_globals, audio_module_globals_table);

const mp_obj_module_t audio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&audio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_audio, audio_module);
