#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_tts.h"
#include "esp_tts_voice_xiaole.h"
#include "esp_tts_voice_template.h"
#include "esp_tts_player.h"
#include "unity.h"

#include "esp_partition.h"
#include "esp_idf_version.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_ae_ch_cvt.h"

static const char *TAG = "AUDIO_TTS";
static esp_tts_handle_t *g_tts_handle = NULL;

volatile bool is_tts_initialized = false;
SemaphoreHandle_t tts_semaphore;

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

        esp_tts_voice_t *voice = esp_tts_voice_set_init(&esp_tts_voice_template, (int16_t *)voicedata);
        if (voice == NULL) {
            ESP_LOGE(TAG, "TTS voice init failed!");
            return;
        }
        g_tts_handle = esp_tts_create(voice);
        tts_semaphore = xSemaphoreCreateBinary();
        assert(tts_semaphore != NULL);
        xSemaphoreGive(tts_semaphore);
        is_tts_initialized = true;
    }
}

static void text_to_speech_task(void *arg)
{
    const char *text = (const char *)arg;
    if (g_tts_handle == NULL) {
        ESP_LOGE(TAG, "TTS model not initialized!\n");
        goto end;
    }

    xSemaphoreTake(tts_semaphore, portMAX_DELAY); //不允许语音合成时，其它任务再调用语音合成。
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();
    // esp_codec_dev_set_out_vol(codec_dev, 80);

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
        int len;
        do {
            short *pcm = esp_tts_stream_play(g_tts_handle, &len, 0);
            if(len > 0){
                esp_ae_ch_cvt_process(c_handle, len, (void *)pcm, buffer);
                esp_codec_dev_write(codec_dev, (int8_t *)buffer, len * 4);
            }
        } while (len > 0);
    }

    esp_tts_stream_reset(g_tts_handle);
    esp_ae_ch_cvt_close(c_handle);
    free(buffer);
    xSemaphoreGive(tts_semaphore);
end:
    vTaskDelete(NULL);
}

void text_to_speech(const char *text)
{
    if(is_tts_initialized){
        xTaskCreatePinnedToCore(text_to_speech_task, "tts_task", 4*1024, (void *)text, 5, NULL, 0);
    }else{
        ESP_LOGW(TAG, "Please init tts module first.");
    }
}

