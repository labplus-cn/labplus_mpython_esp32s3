/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
#define QUIT_CMD_FOUND (BIT0)

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
    ESP_LOGE(TAG, "esp_gmf_afe_event_cb..................");
    switch (event->type) {
        case ESP_GMF_AFE_EVT_WAKEUP_START: {
            wakeup = true;
#if WAKENET_ENABLE == true && VCMD_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
            esp_gmf_afe_vcmd_detection_begin(obj);
#endif  /* WAKENET_ENABLE == true && VCMD_ENABLE == true */
            if (event->event_data) {
                esp_gmf_afe_wakeup_info_t *info = event->event_data;
                ESP_LOGI(TAG, "WAKEUP_START [%d : %d]", info->wake_word_index, info->wakenet_model_index);
                
                model_init();
	            text_to_speech(wakeup_word);
            } else {
                ESP_LOGI(TAG, "WAKEUP_START");
            }
            break;
        }
        case ESP_GMF_AFE_EVT_WAKEUP_END: {
            wakeup = false;
#if WAKENET_ENABLE == true && VCMD_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
#endif  /* WAKENET_ENABLE == true && VCMD_ENABLE == true */
            ESP_LOGI(TAG, "WAKEUP_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_START: {
#if WAKENET_ENABLE != true && VCMD_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
            esp_gmf_afe_vcmd_detection_begin(obj);
#endif  /* WAKENET_ENABLE != true && VCMD_ENABLE == true */
            speeching = true;
            ESP_LOGI(TAG, "VAD_START");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_END: {
#if WAKENET_ENABLE != true && VCMD_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
#endif  /* WAKENET_ENABLE != true && VCMD_ENABLE == true */
            speeching = false;
            ESP_LOGI(TAG, "VAD_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT: {
            ESP_LOGI(TAG, "VCMD_DECT_TIMEOUT");
            break;
        }
        default: {
            esp_gmf_afe_vcmd_info_t *info = event->event_data;
            ESP_LOGW(TAG, "Command %d, phrase_id %d, prob %f, str: %s",
                     event->type, info->phrase_id, info->prob, info->str);
            latest_command_id = event->type;
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
    int tmp;
    for(int i = 0; i < load->valid_size; i++){
        tmp = load->buf[i];
    }
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
        memcpy(load->buf, &audio_data[offset], wanted_size);
        offset += wanted_size;
        load->valid_size = wanted_size;

        if (offset == total_size) {
            offset = 0;
            load->is_done = true;
        }
    } else {
        load->valid_size = 0;
        load->is_done = true;
    }
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
    xEventGroupSetBits(g_event_group, QUIT_CMD_FOUND);
    return 0;
}

static void sr_task(void *arg)
{
    g_event_group = xEventGroupCreate();

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_all_defaults(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"ai_afe"};
    esp_gmf_pool_new_pipeline(pool, NULL, name, sizeof(name) / sizeof(char *), NULL, &pipe);
    if (pipe == NULL) {
        ESP_LOGE(TAG, "There is no pipeline");
        goto __quit;
    }
    // esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_IN_INSTANCE(pipe), esp_gmf_app_get_record_handle());

    esp_gmf_pipeline_get_el_by_name(pipe, "ai_afe", &g_afe);
    ESP_LOGI(TAG, "Create AFE, %s-%p", OBJ_GET_TAG((esp_gmf_obj_t *)g_afe), (esp_gmf_obj_t *)g_afe);
    esp_gmf_afe_set_event_cb(g_afe, esp_gmf_afe_event_cb, NULL);

    esp_gmf_port_handle_t inport = NEW_ESP_GMF_PORT_IN_BYTE(afe_acquire_read, afe_release_read, NULL, NULL, 2048, 100);
    esp_gmf_pipeline_reg_el_port(pipe, name[0], ESP_GMF_IO_DIR_READER, inport);
    esp_gmf_port_handle_t outport = NEW_ESP_GMF_PORT_OUT_BYTE(outport_acquire_write, outport_release_write, NULL, NULL, 2048, 100);
    esp_gmf_pipeline_reg_el_port(pipe, name[0], ESP_GMF_IO_DIR_WRITER, outport);

    esp_gmf_info_sound_t info = {
        .sample_rates = 16000,
        .channels = 1,
        .bits = 16,
    };
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
        EventBits_t bits = xEventGroupWaitBits(g_event_group, QUIT_CMD_FOUND, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & QUIT_CMD_FOUND) {
            ESP_LOGI(TAG, "Quit command found, stopping pipeline");
            break;
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