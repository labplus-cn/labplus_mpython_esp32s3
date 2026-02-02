#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_tts.h"
#include "esp_tts_voice_xiaole.h"
#include "esp_tts_voice_template.h"
#include "esp_tts_player.h"
// #include "ringbuf.h"
#include "unity.h"

#include "esp_partition.h"
#include "esp_idf_version.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_ae_ch_cvt.h"

static const char *TAG = "AUDIO_TTS";
static esp_tts_handle_t *g_tts_handle = NULL;
SemaphoreHandle_t tts_semaphore;

volatile int tts_flag = 0;
volatile bool is_tts_initialized = false;

extern BaseType_t xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,
                                             const char * const pcName,
                                             const uint32_t usStackDepth,
                                             void * const pvParameters,
                                             UBaseType_t uxPriority,
                                             TaskHandle_t * const pxCreatedTask,
                                             const BaseType_t xCoreID );

void model_init(void)
{
    if(!is_tts_initialized){
        const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "voice_data");
        if (part == NULL) {
            ESP_LOGE(TAG, "Couldn't find voice data partition!\n");
            return;
        }

        const void *voicedata;
        esp_partition_mmap_handle_t mmap;
        esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, (const void **)&voicedata, &mmap);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Couldn't map voice data partition!\n");
            return;
        }

        // 初始化语音模型，使用模板初始化
        esp_tts_voice_t *voice = esp_tts_voice_set_init(&esp_tts_voice_template, (int16_t *)voicedata);
        if (voice == NULL) {
            ESP_LOGE(TAG, "TTS voice init failed!\n");
            return;
        }
        g_tts_handle = esp_tts_create(voice);
        is_tts_initialized=1;
    }
}

static void text_to_speech_task(void *arg)
{
    const char *text = (const char *)arg;
    if (g_tts_handle == NULL) {
        ESP_LOGE(TAG, "TTS model not initialized!\n");
        xSemaphoreGive(tts_semaphore);
        vTaskDelete(NULL);
        return;
    }

    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();
    esp_codec_dev_set_out_vol(codec_dev, 80);

    esp_ae_ch_cvt_cfg_t config;
    float w_data[2] = {1.0, 1.0};
    config.sample_rate = 16000;
    config.src_ch = 1;
    config.dest_ch = 2;
    config.weight = w_data;
    config.weight_len = 2;
    config.bits_per_sample = 16;
    void *c_handle = NULL;
    int ret = esp_ae_ch_cvt_open(&config, &c_handle);
    TEST_ASSERT_NOT_EQUAL(c_handle, NULL);
    char *buffer = calloc(sizeof(int), 1024);
    TEST_ASSERT_NOT_EQUAL(buffer, NULL);

    if (esp_tts_parse_chinese(g_tts_handle, text)) {
        tts_flag = 1;
        int len;

        do {
            short *pcm = esp_tts_stream_play(g_tts_handle, &len, 0);
            esp_ae_ch_cvt_process(c_handle, len, (void *)pcm, buffer);
            esp_codec_dev_write(codec_dev, (int8_t *)buffer, len * 4);
        } while (len > 0);

        tts_flag = 0;
    }

    esp_tts_stream_reset(g_tts_handle);
    xSemaphoreGive(tts_semaphore);
    esp_ae_ch_cvt_close(c_handle);
    free(buffer);
    vTaskDelete(NULL);
}

void text_to_speech(const char *text)
{
    // sc_stop_flag = 1;
    tts_semaphore = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(text_to_speech_task, "tts_task", 4*1024, (void *)text, 5, NULL, 0);
    xSemaphoreTake(tts_semaphore, portMAX_DELAY);
    // sc_stop_flag = 0;
    vSemaphoreDelete(tts_semaphore);
}

int get_tts_flag(void) {
    return tts_flag;
}

int get_is_tts_initialized(void) {
    return is_tts_initialized;
}
