/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_gmf_element.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_port.h"
#include "esp_gmf_job.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"

#include "esp_gmf_wn.h"
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#include "esp_afe_config.h"
#include "esp_gmf_afe_manager.h"
#include "esp_gmf_afe.h"
#include "esp_gmf_aec.h"
#endif  /* defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4) */
#include "esp_gmf_app_setup_peripheral.h"
#include "audio_data.c"

#define DEBUG2FILE  (false)
#define FS          16000
#define DURATION    1
#define SIGNAL_LEN  (FS * DURATION)
#define STEREO_LEN  (SIGNAL_LEN * 2)
#define DELAY       0
#define ATTENUATION 0.1

#define WAKEUP_DETECTED (BIT0)
#define VAD_DETECTED    (BIT1)
#define VCMD_FOUND      (BIT2)
#ifdef CONFIG_IDF_TARGET_ESP32
#define AEC_ENABLE    (false)
#define MN_ENABLE     (false)
#define EVENTS_2_WAIT (WAKEUP_DETECTED | VAD_DETECTED)
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define AEC_ENABLE    (true)
#define MN_ENABLE     (true)
#define EVENTS_2_WAIT (WAKEUP_DETECTED | VAD_DETECTED | VCMD_FOUND)
#else
#define AEC_ENABLE    (false)
#define MN_ENABLE     (false)
#define EVENTS_2_WAIT (WAKEUP_DETECTED)
#endif  /* CONFIG_IDF_TARGET_ESP32 */

static const char        *TAG           = "AI_AUDIO_TEST";
static uint32_t           out_count     = 0;
static EventGroupHandle_t g_event_group = NULL;

/*
static esp_gmf_err_io_t wn_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
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

static esp_gmf_err_io_t wn_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    load->valid_size = 0;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t wn_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    if (load->buf == NULL) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t wn_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    return ESP_GMF_ERR_OK;
}

static void esp_gmf_wn_event_cb(esp_gmf_obj_handle_t obj, int32_t trigger_ch, void *user_ctx)
{
    static int32_t cnt = 1;
    ESP_LOGI(TAG, "WWE detected on channel %" PRIu32 ", cnt: %" PRIi32, trigger_ch, cnt++);
    xEventGroupSetBits(g_event_group, WAKEUP_DETECTED);
}

// TEST_CASE("Test gmf wakenet process", "[ESP_GMF_WN]")
void test(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// Wakenet /////////////////////\r\n");
    g_event_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(g_event_group);

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(wn_acquire_read, wn_release_read, NULL, NULL, 1024, 100);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(wn_acquire_write, wn_release_write, NULL, NULL, 1024, 100);

    srmodel_list_t *models = esp_srmodel_init("model");
    const char *ch_format = "M";
    esp_gmf_wn_cfg_t wn_cfg = {
        .input_format = (char *)ch_format,
        .det_mode = DET_MODE_95,
        .models = models,
        .detect_cb = esp_gmf_wn_event_cb,
        .user_ctx = NULL,
    };
    esp_gmf_element_handle_t gmf_wn = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_wn_init(&wn_cfg, &gmf_wn));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(gmf_wn, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(gmf_wn, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(gmf_wn, NULL));
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    do {
        ret = esp_gmf_element_process_running(gmf_wn, NULL);
        if (ret == ESP_GMF_JOB_ERR_FAIL) {
            ESP_LOGE(TAG, "WN process failed");
            break;
        } else if (ret == ESP_GMF_JOB_ERR_DONE) {
            ESP_LOGI(TAG, "WN process done");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (true);
    TEST_ASSERT_EQUAL(WAKEUP_DETECTED, xEventGroupWaitBits(g_event_group, WAKEUP_DETECTED, pdTRUE, pdTRUE, pdMS_TO_TICKS(10 * 1000)));
    esp_gmf_element_process_close(gmf_wn, NULL);
    esp_gmf_obj_delete(gmf_wn);
    esp_srmodel_deinit(models);
    vEventGroupDelete(g_event_group);
    g_event_group = NULL;
}*/

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

static esp_gmf_err_io_t afe_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    if (load->buf == NULL) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

void esp_gmf_afe_event_cb1(esp_gmf_obj_handle_t obj, esp_gmf_afe_evt_t *event, void *user_data)
{
    switch (event->type) {
        case ESP_GMF_AFE_EVT_WAKEUP_START: {
            xEventGroupSetBits(g_event_group, WAKEUP_DETECTED);
#if MN_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
            esp_gmf_afe_vcmd_detection_begin(obj);
#endif  /* MN_ENABLE == true */
            esp_gmf_afe_wakeup_info_t *info = event->event_data;
            ESP_LOGI(TAG, "WAKEUP_START [%d : %d]", info->wake_word_index, info->wakenet_model_index);
            break;
        }
        case ESP_GMF_AFE_EVT_WAKEUP_END: {
#if MN_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
#endif  /* MN_ENABLE == true */
            ESP_LOGI(TAG, "WAKEUP_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_START: {
            xEventGroupSetBits(g_event_group, VAD_DETECTED);
            ESP_LOGI(TAG, "VAD_START");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_END: {
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
            if (event->type == 25 || event->type == 216) {
                // 25: "da kai kong tiao"
                // 216: "kai kong tiao"
                xEventGroupSetBits(g_event_group, VCMD_FOUND);
            }
            break;
        }
    }
}

// TEST_CASE("Test gmf afe process", "[ESP_GMF_AFE]")
void test(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// AFE /////////////////////\r\n");
    g_event_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(g_event_group);

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(afe_acquire_read, afe_release_read, NULL, NULL, 1024, 100);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(afe_acquire_write, afe_release_write, NULL, NULL, 1024, 100);

    esp_gmf_afe_manager_handle_t afe_manager = NULL;
    srmodel_list_t *models = esp_srmodel_init("model");
    const char *ch_format = "M";
    afe_config_t *afe_cfg = afe_config_init(ch_format, models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_cfg->vad_init = true;
    afe_cfg->vad_mode = VAD_MODE_2;
    afe_cfg->vad_min_speech_ms = 64;
    afe_cfg->vad_min_noise_ms = 100;
    afe_cfg->wakenet_init = true;
    afe_cfg->aec_init = AEC_ENABLE;
    esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg, NULL, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_create(&afe_manager_cfg, &afe_manager));
    esp_gmf_element_handle_t gmf_afe = NULL;
    esp_gmf_afe_cfg_t gmf_afe_cfg = DEFAULT_GMF_AFE_CFG(afe_manager, esp_gmf_afe_event_cb1, NULL, models);
    gmf_afe_cfg.vcmd_detect_en = MN_ENABLE;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_init(&gmf_afe_cfg, &gmf_afe));
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t *out_caps = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_caps(gmf_afe, (const esp_gmf_cap_t **)&caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_AEC, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_NS, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_AGC, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_VAD, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_WWE, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_VCMD, &out_caps));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(gmf_afe, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(gmf_afe, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(gmf_afe, NULL));
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    do {
        ret = esp_gmf_element_process_running(gmf_afe, NULL);
        if (ret == ESP_GMF_JOB_ERR_FAIL) {
            ESP_LOGE(TAG, "AFE process failed");
            break;
        } else if (ret == ESP_GMF_JOB_ERR_DONE) {
            ESP_LOGI(TAG, "AFE process done");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (true);
    TEST_ASSERT_EQUAL(EVENTS_2_WAIT, xEventGroupWaitBits(g_event_group, EVENTS_2_WAIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(10 * 1000)));
    esp_gmf_element_process_close(gmf_afe, NULL);
    esp_gmf_obj_delete(gmf_afe);
    afe_config_free(afe_cfg);
    esp_gmf_afe_manager_destroy(afe_manager);
    esp_srmodel_deinit(models);
    vEventGroupDelete(g_event_group);
    g_event_group = NULL;
}