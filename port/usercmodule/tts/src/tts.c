#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include "esp_tts.h"
#include "esp_tts_voice_xiaole.h"
#include "esp_tts_voice_template.h"
#include "esp_tts_player.h"
#include "ringbuf.h"

#include "esp_partition.h"
#include "esp_idf_version.h"
#include "bsp_audio.h"

typedef struct {
    char text[256];
    SemaphoreHandle_t done_semaphore;
} tts_task_param_t;

volatile int tts_flag = 0;

void tts_task(void* param) {
    tts_task_param_t* task_param = (tts_task_param_t*)param;
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "voice_data");
    if (part == NULL) {
        printf("Couldn't find voice data partition!\n");
        vTaskDelete(NULL);
        return;
    }

    const void* voicedata;
    esp_partition_mmap_handle_t mmap;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, (const void**)&voicedata, &mmap);
    if (err != ESP_OK) {
        printf("Couldn't map voice data partition!\n");
        vTaskDelete(NULL);
        return;
    }

    esp_tts_voice_t* voice = esp_tts_voice_set_init(&esp_tts_voice_template, (int16_t*)voicedata);
    if (voice == NULL) {
        printf("TTS voice init failed!\n");
        return;
    }
    esp_tts_handle_t* tts_handle = esp_tts_create(voice);

    if (esp_tts_parse_chinese(tts_handle, task_param->text)) {
        tts_flag=1;
        int len[1]={0};

        bsp_codec_dev_delete();
        bsp_codec_dev_create();
        bsp_audio_set_play_vol(50);
        bsp_codec_dev_open(16000,2,32);
        do {
            short* pcm = esp_tts_stream_play(tts_handle, len, 2);
            bsp_audio_play2(pcm, len[0]*2, portMAX_DELAY);
        } while (len[0] > 0);
        bsp_codec_dev_close();
        bsp_codec_dev_open(16000,1,16);
        tts_flag=0;
    }

    esp_tts_stream_reset(tts_handle);
    xSemaphoreGive(task_param->done_semaphore);
    vTaskDelete(NULL);
}

void text_to_speech(const char* text) {
    static SemaphoreHandle_t done_semaphore = NULL;
    if (done_semaphore == NULL) {
        done_semaphore = xSemaphoreCreateBinary();
    }

    tts_task_param_t* task_param = malloc(sizeof(tts_task_param_t));
    if (!task_param) {
        printf("Failed to allocate memory for task parameters\n");
        return;
    }

    strncpy(task_param->text, text, sizeof(task_param->text) - 1);
    task_param->text[sizeof(task_param->text) - 1] = '\0';
    task_param->done_semaphore = done_semaphore;

    xTaskCreatePinnedToCore(tts_task, "tts_task", 8192, task_param, 5, NULL, 0);
    xSemaphoreTake(done_semaphore, portMAX_DELAY);
    free(task_param);
}
int get_tts_flag(void) {
    return tts_flag;
}