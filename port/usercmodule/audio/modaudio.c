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

/* Capture context */
typedef struct {
    esp_capture_handle_t capture;
    esp_capture_sink_handle_t sink;
    esp_capture_audio_src_if_t *audio_src;
} capture_ctx_t;

/* Pipeline event callback to configure ALC gain after pipeline is built */
static esp_capture_err_t pipeline_event_cb(esp_capture_event_t event, void *ctx)
{
    if (event != ESP_CAPTURE_EVENT_AUDIO_PIPELINE_BUILT) {
        return ESP_CAPTURE_ERR_OK;
    }

    esp_capture_sink_handle_t sink = (esp_capture_sink_handle_t)ctx;
    esp_gmf_element_handle_t alc_el = NULL;

    if (esp_capture_sink_get_element_by_tag(sink, ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                            "aud_alc", &alc_el) == ESP_CAPTURE_ERR_OK) {
        esp_gmf_alc_set_gain(alc_el, 0, 63);
        esp_gmf_alc_set_gain(alc_el, 1, 63);
    }

    return ESP_CAPTURE_ERR_OK;
}

/* Setup esp_capture with encoder based on format */
static esp_err_t setup_capture(capture_ctx_t *ctx, const char *format,
                                const audio_format_t *fmt)
{
    // Register OPUS encoder if needed
    if (strcmp(format, "opus") == 0) {
        esp_opus_enc_register();
    }

    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    if (!codec_dev) {
        return ESP_FAIL;
    }

    // Create audio source
    esp_capture_audio_dev_src_cfg_t src_cfg = {
        .record_handle = codec_dev,
    };
    ctx->audio_src = esp_capture_new_audio_dev_src(&src_cfg);
    if (!ctx->audio_src) {
        return ESP_FAIL;
    }

    // Set fixed capabilities (codec provides stereo 16kHz 16-bit)
    esp_capture_audio_info_t fixed = {
        .format_id = ESP_CAPTURE_FMT_ID_PCM,
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    ctx->audio_src->set_fixed_caps(ctx->audio_src, &fixed);

    // Open capture
    esp_capture_cfg_t cap_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_NONE,
        .audio_src = ctx->audio_src,
        .video_src = NULL,
    };
    if (esp_capture_open(&cap_cfg, &ctx->capture) != ESP_OK) {
        return ESP_FAIL;
    }

    // Setup sink based on format
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .sample_rate = fmt->sample_rate,
            .channel = fmt->channels,
            .bits_per_sample = fmt->bits_per_sample,
        },
    };

    // Set format ID based on format
    if (strcmp(format, "opus") == 0) {
        sink_cfg.audio_info.format_id = ESP_CAPTURE_FMT_ID_OPUS;
    } else {
        // WAV and MP3 use PCM (MP3 encoder not supported by esp_capture)
        sink_cfg.audio_info.format_id = ESP_CAPTURE_FMT_ID_PCM;
    }

    if (esp_capture_sink_setup(ctx->capture, 0, &sink_cfg, &ctx->sink) != ESP_OK) {
        esp_capture_close(ctx->capture);
        return ESP_FAIL;
    }

    // Register ALC element into capture pool before building pipeline
    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    esp_gmf_element_handle_t alc_hd = NULL;
    if (esp_gmf_alc_init(&alc_cfg, &alc_hd) == ESP_GMF_ERR_OK) {
        if (esp_capture_register_element(ctx->capture, ESP_CAPTURE_STREAM_TYPE_AUDIO, alc_hd) != ESP_CAPTURE_ERR_OK) {
            esp_gmf_obj_delete(alc_hd);
            alc_hd = NULL;
        }
    }

    // Set event callback BEFORE building pipeline (event fires during esp_capture_start)
    esp_capture_set_event_cb(ctx->capture, pipeline_event_cb, ctx->sink);

    // Build pipeline based on format
    const char *elements[3];
    int num_elements = 0;

    // Channel conversion (stereo to mono/stereo)
    elements[num_elements++] = "aud_ch_cvt";

    // Add ALC for volume boost only if registration succeeded
    if (alc_hd != NULL) {
        elements[num_elements++] = "aud_alc";
    }

    // Add encoder only for opus
    if (strcmp(format, "opus") == 0) {
        elements[num_elements++] = "aud_enc";
    }

    if (num_elements > 0) {
        if (esp_capture_sink_build_pipeline(ctx->sink, ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                           elements, num_elements) != ESP_OK) {
            esp_capture_close(ctx->capture);
            return ESP_FAIL;
        }
    }

    // Enable sink
    esp_capture_sink_enable(ctx->sink, ESP_CAPTURE_RUN_MODE_ALWAYS);

    return ESP_OK;
}

/* codec固定配置：audio_init()以16kHz/2ch/16bit打开codec */
#define CODEC_SAMPLE_RATE      16000
#define CODEC_CHANNELS         2
#define CODEC_BITS_PER_SAMPLE  16
#define RECORD_CHUNK_SAMPLES   512   /* 每次从codec读取的每通道采样点数 */

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

/* ------------------- 录音 ------------------- */

/* loudness后台任务 */
static volatile uint16_t  g_loudness        = 0;
static volatile bool      g_loudness_run    = false;

static void loudness_task(void *arg)
{
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    int buf_bytes = RECORD_CHUNK_SAMPLES * CODEC_CHANNELS * (CODEC_BITS_PER_SAMPLE / 8);
    int16_t *buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    while (g_loudness_run) {
        esp_codec_dev_read(codec_dev, buf, buf_bytes);
        int max_amp = 0;
        int total = RECORD_CHUNK_SAMPLES * CODEC_CHANNELS;
        for (int i = 0; i < total; i++) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > max_amp) max_amp = v;
        }
        /* 归一化到 0~100 */
        g_loudness = (uint16_t)(max_amp * 100 / 32768);
    }
    heap_caps_free(buf);
    vTaskDelete(NULL);
}

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
    if (!g_loudness_run) {
        g_loudness_run = true;
        g_loudness = 0;
        xTaskCreatePinnedToCore(loudness_task, "loudness", 4096, NULL, 5, NULL, 0);
    }
    return MP_OBJ_NEW_SMALL_INT(g_loudness);
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_loudness_obj, audio_loudness);

static mp_obj_t audio_loudness_stop(void)
{
    g_loudness_run = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    g_loudness = 0;
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

    // Check if file_name starts with "file://littlefs/"
    const char *prefix = "file://littlefs/";
    if (strncmp(file_name, prefix, strlen(prefix)) != 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("file_name must start with 'file://littlefs/'"));
    }
    // Strip "file:/" prefix for fopen
    const char *actual_path = file_name + 6;  // Skip "file:/"

    // Auto-detect format from filename
    const char *format = detect_format_from_filename(file_name);

    // Setup audio format
    audio_format_t fmt = {
        .sample_rate = target_rate,
        .channels = target_ch,
        .bits_per_sample = bits_per_sample,
    };

    // Setup esp_capture
    capture_ctx_t cap_ctx = {0};
    if (setup_capture(&cap_ctx, format, &fmt) != ESP_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to setup capture"));
    }

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
        esp_capture_close(cap_ctx.capture);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to open file"));
    }

    /* Initialize format writer */
    uint8_t writer_buf[256];
    writer_ops->open((format_writer_handle_t)writer_buf, f, &fmt);

    /* Start capture */
    if (esp_capture_start(cap_ctx.capture) != ESP_OK) {
        fclose(f);
        esp_capture_close(cap_ctx.capture);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to start capture"));
    }

    /* Capture and write frames */
    TickType_t start_time = xTaskGetTickCount();
    TickType_t duration_ticks = pdMS_TO_TICKS(record_time * 1000);

    while ((xTaskGetTickCount() - start_time) < duration_ticks) {
        esp_capture_stream_frame_t frame = {0};
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;

        // Acquire frame from capture sink (false = blocking)
        if (esp_capture_sink_acquire_frame(cap_ctx.sink, &frame, false) == ESP_CAPTURE_ERR_OK) {
            if (frame.data && frame.size > 0) {
                writer_ops->write((format_writer_handle_t)writer_buf,
                                 frame.data, frame.size);
            }
            esp_capture_sink_release_frame(cap_ctx.sink, &frame);
        }
    }

    /* Stop capture */
    esp_capture_stop(cap_ctx.capture);

    /* Finalize format writer */
    writer_ops->close((format_writer_handle_t)writer_buf);

    /* Cleanup */
    fclose(f);
    esp_capture_close(cap_ctx.capture);

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
