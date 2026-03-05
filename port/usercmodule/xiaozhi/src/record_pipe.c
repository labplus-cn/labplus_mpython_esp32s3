/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_gmf_io.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_app_setup_peripheral.h"

#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_app_cli.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_afe.h"
#include "sr.h"
#include "tts.h"
#include "audio_data.c"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_ch_cvt.h"
#define VOICE2FILE     (false)

#ifdef CONFIG_GMF_AI_AUDIO_WAKEUP_ENABLE
#define WAKENET_ENABLE (true)
#else
#define WAKENET_ENABLE (false)
#endif /* CONFIG_GMF_AI_AUDIO_WAKEUP_ENABLE */
#ifdef CONFIG_GMF_AI_AUDIO_VOICE_COMMAND_ENABLE
#define VCMD_ENABLE (true)
#else
#define VCMD_ENABLE (false)
#endif /* CONFIG_GMF_AI_AUDIO_VOICE_COMMAND_ENABLE */
#define EV_QUIT (BIT0)
#define EV_TTS         (BIT1)

static const char *TAG = "AI_AUDIO_WWE";

static bool speeching = false;
static bool wakeup    = false;
static EventGroupHandle_t g_event_group = NULL;
static esp_gmf_element_handle_t g_afe   = NULL;
volatile int latest_command_id = 0;
static char wakeup_word[64] = "";

extern BaseType_t xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,
                                             const char * const pcName,
                                             const uint32_t usStackDepth,
                                             void * const pvParameters,
                                             UBaseType_t uxPriority,
                                             TaskHandle_t * const pxCreatedTask,
                                             const BaseType_t xCoreID );

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return 0;
}

void esp_gmf_afe_event_cb(esp_gmf_obj_handle_t obj, esp_gmf_afe_evt_t *event, void *user_data)
{
    switch (event->type) {
        case ESP_GMF_AFE_EVT_WAKEUP_START: {
            wakeup = true;
            if (event->event_data) {
                esp_gmf_afe_wakeup_info_t *info = event->event_data;
                ESP_LOGI(TAG, "WAKEUP_START [%d : %d]", info->wake_word_index, info->wakenet_model_index);
                // xEventGroupSetBits(g_event_group, EV_TTS);
            } else {
                ESP_LOGI(TAG, "WAKEUP_START");
            }
            break;
        }
        case ESP_GMF_AFE_EVT_WAKEUP_END: {
            wakeup = false;
            ESP_LOGI(TAG, "WAKEUP_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_START: {
            speeching = true;
            ESP_LOGI(TAG, "VAD_START");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_END: {
            speeching = false;
            ESP_LOGI(TAG, "VAD_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT: {
            ESP_LOGI(TAG, "VCMD_DECT_TIMEOUT");
            break;
        }
        default: {
            break;
        }
    }
}

static void voice_2_file(uint8_t *buffer, int len)
{
#if VOICE2FILE == true
#define MAX_FNAME_LEN (50)

    static FILE *fp = NULL;
    static int fcnt = 0;

    if (speeching) {
        if (!fp) {
            char fname[MAX_FNAME_LEN] = {0};
            snprintf(fname, MAX_FNAME_LEN - 1, "/sdcard/16k_16bit_1ch_%d.pcm", fcnt++);
            fp = fopen(fname, "wb");
            if (!fp) {
                ESP_LOGE(TAG, "File open failed");
                return;
            }
        }
        if (len) {
            fwrite(buffer, len, 1, fp);
        }
    } else {
        if (fp) {
            ESP_LOGI(TAG, "File closed");
            fclose(fp);
            fp = NULL;
        }
    }
#endif  /* VOICE2FILE == true && WITH_AFE == true */
}

static esp_gmf_err_io_t outport_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    ESP_LOGD(TAG, "Acquire write");
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t outport_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    ESP_LOGD(TAG, "Release write");
    voice_2_file(load->buf, load->valid_size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    static int offset = 0;
    int total_size = 59600;
    if (offset < total_size) {
        if (offset + wanted_size > total_size) {
            wanted_size = total_size - offset;
        }
        ESP_LOGD(TAG, "afe_acquire_read wanted:%d offset:%d", wanted_size, offset);
        memcpy(load->buf, &audio_data[offset], wanted_size);
        offset += wanted_size;
        load->valid_size = wanted_size;
        ESP_LOGD(TAG, "afe_acquire_read loaded:%d new_offset:%d", load->valid_size, offset);
        if (offset == total_size) {
            /* loop audio instead of marking done so AFE receives continuous stream */
            offset = 0;
            /* do not set load->is_done to allow continuous feeding */
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    } else {
        load->valid_size = 0;
        /* keep streaming: do not mark done */
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    load->valid_size = 0;
    return ESP_GMF_IO_OK;
}

static int keep_awake(bool en)
{
    if (g_afe) {
        esp_gmf_afe_keep_awake(g_afe, en);
    } else {
        ESP_LOGE(TAG, "AFE not found");
    }
    return ESP_OK;
}

static int trigger(bool en)
{
    if (g_afe) {
        if (en) {
            esp_gmf_afe_trigger_wakeup(g_afe);
        } else {
            esp_gmf_afe_trigger_sleep(g_afe);
        }
    }
    return 0;
}

static int quit(void)
{
    xEventGroupSetBits(g_event_group, EV_QUIT);
    return 0;
}

static void sr_task(void *arg)
{
    // esp_log_level_set("AFE_MANAGER", ESP_LOG_DEBUG);
    esp_log_level_set("AFE", ESP_LOG_DEBUG);
    g_event_group = xEventGroupCreate();

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    // gmf_loader_setup_all_defaults(pool);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_io_handle_t dev = NULL;
    codec_dev_io_cfg_t rx_codec_dev_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    rx_codec_dev_cfg.dir = ESP_GMF_IO_DIR_READER;
    rx_codec_dev_cfg.dev = NULL;
    ret = esp_gmf_io_codec_dev_init(&rx_codec_dev_cfg, &dev);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {}, "Failed to init codec dev io");
    ret = esp_gmf_pool_register_io(pool, dev, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_io_deinit(dev); {};}, "Failed to register codec dev io");

    esp_gmf_element_handle_t ch_cvt = NULL;
    esp_ae_ch_cvt_cfg_t es_ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    es_ch_cvt_cfg.sample_rate = 16000;
    es_ch_cvt_cfg.bits_per_sample = 16;
    es_ch_cvt_cfg.src_ch = 2;
    es_ch_cvt_cfg.dest_ch = 1;
    ret = esp_gmf_ch_cvt_init(&es_ch_cvt_cfg, &ch_cvt);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {}, "Failed to init audio ch cvt");
    ret = esp_gmf_pool_register_element(pool, ch_cvt, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(ch_cvt); {};}, "Failed to register element in pool");

    // gmf_loader_setup_ai_audio_default(pool);
    esp_gmf_afe_manager_handle_t afe_manager = NULL;
    srmodel_list_t *models = esp_srmodel_init("model");
    const char *ch_format = "M";
    afe_config_t *afe_cfg = afe_config_init(ch_format, models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_cfg->aec_init = false;
    afe_cfg->pcm_config.total_ch_num = 2; 
    afe_cfg->pcm_config.mic_num = 2; 
    afe_cfg->pcm_config.ref_num = 0;      
    afe_cfg->pcm_config.sample_rate = 16000;
    esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg, NULL, NULL, NULL, NULL);
    /* prioritize fetch task to reduce latency of result processing */
    afe_manager_cfg.fetch_task_setting.prio = 10;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_create(&afe_manager_cfg, &afe_manager));
    esp_gmf_afe_cfg_t gmf_afe_cfg = DEFAULT_GMF_AFE_CFG(afe_manager, esp_gmf_afe_event_cb, NULL, models);
    gmf_afe_cfg.vcmd_detect_en = false;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_init(&gmf_afe_cfg, &g_afe));
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t *out_caps = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_caps(g_afe, (const esp_gmf_cap_t **)&caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_AEC, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_NS, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_AGC, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_VAD, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_WWE, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_VCMD, &out_caps));
    esp_gmf_err_t ret1 = esp_gmf_pool_register_element(pool, g_afe, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret1, {esp_gmf_element_deinit(g_afe); {};}, "Failed to register element in pool");

    esp_gmf_pipeline_handle_t pipe = NULL;
    // const char *name[] = {"aud_ch_cvt", "ai_afe"};
    const char *name[] = {"ai_afe"};
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), NULL, &pipe);
    if (pipe == NULL) {
        ESP_LOGE(TAG, "There is no pipeline");
        goto __quit;
    }
    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_IN_INSTANCE(pipe), esp_gmf_app_get_record_handle());
    // esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(pipe), esp_gmf_app_get_playback_handle());

    esp_gmf_pipeline_get_el_by_name(pipe, "ai_afe", &g_afe);
    esp_gmf_afe_set_event_cb(g_afe, esp_gmf_afe_event_cb, NULL);

    // esp_gmf_port_handle_t inport = NEW_ESP_GMF_PORT_IN_BYTE(afe_acquire_read, afe_release_read, NULL, NULL, 2048, 100);
    // esp_gmf_pipeline_reg_el_port(pipe, name[0], ESP_GMF_IO_DIR_READER, inport);
    esp_gmf_port_handle_t outport = NEW_ESP_GMF_PORT_OUT_BYTE(outport_acquire_write, outport_release_write, NULL, NULL, 2048, 100);
    esp_gmf_pipeline_reg_el_port(pipe, name[0], ESP_GMF_IO_DIR_WRITER, outport);

    esp_gmf_info_sound_t info = {
        .sample_rates = 16000,
        .channels = 2,
        .bits = 16,
    };
    // 这个函数用于向 GMF 管道的第一个元素报告音频/视频信息
    // 信息传递机制：将媒体信息（如音频采样率、通道数、位深等）从管道外部传入到管道的第一个元素
    // 触发级联处理：当管道中的某些元素（如采样率转换、通道转换等）需要这些信息来初始化或配置时，通过此函数报告这些信息
    // 支持多种信息类型：
    // ESP_GMF_INFO_SOUND：音频信息（采样率、通道、位深等）
    // ESP_GMF_INFO_VIDEO：视频信息
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.core = 0;
    cfg.thread.prio = 5;
    cfg.thread.stack = 5120;
    esp_gmf_task_handle_t task = NULL;
    esp_gmf_task_init(&cfg, &task);
    esp_gmf_pipeline_bind_task(pipe, task);
    esp_gmf_pipeline_loading_jobs(pipe);
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);
    esp_gmf_pipeline_run(pipe);

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(g_event_group, EV_QUIT|EV_TTS, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & EV_QUIT) {
            ESP_LOGI(TAG, "Quit command found, stopping pipeline");
            break;
        }else if(bits & EV_TTS){
            model_init();
            text_to_speech(wakeup_word);
        }
    }

__quit:
    esp_gmf_pipeline_stop(pipe);
    esp_gmf_task_deinit(task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_all_defaults(pool);
    esp_gmf_pool_deinit(pool);
    vEventGroupDelete(g_event_group);
    ESP_LOGW(TAG, "Wake word engine demo finished");
}

int get_latest_command_id(void) {
    return latest_command_id;
}

int get_wakeup_flag(void) {
    return 0;
}

void reset_latest_command_id(void) {
    latest_command_id = 0;
}

void sr_init(const char *word, uint16_t timeout_ms)
{
    if (word && word[0] != '\0') {
        strncpy(wakeup_word, word, sizeof(wakeup_word) - 1);
        wakeup_word[sizeof(wakeup_word) - 1] = '\0';  // 确保字符串以 '\0' 结尾
    }

    xTaskCreatePinnedToCore(sr_task, "sr_task", 4*1024, NULL, 5, NULL, 0);
}