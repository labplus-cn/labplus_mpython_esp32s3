#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
// #include "freertos_tasks_c_additions.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_process_sdkconfig.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"
#include "esp_afe_config.h"

#include "audio_pipeline.h"
#include "filter_resample.h"
#include "raw_stream.h"
#include "i2s_stream.h"

// #include "bsp_audio_board.h"
#include "esp_mn_speech_commands.h"
#include "tts.h"
// #include "wav_codec.h"
#include "speech_recognition.h"
#include "board.h"

int wakeup_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
//static esp_afe_sr_data_t *afe_data = NULL;
static volatile int task_flag = 0;
volatile int latest_command_id = 0;
srmodel_list_t *models = NULL;

static char wakeup_word[64] = "";
static uint16_t timeout  = 6000;

volatile int sr_stop_flag = 0;

#define SILENCE_THRESHOLD 50
// static wav_codec_t *wav_codec = NULL;
// static bool vad_record_flag = false;
static bool recording = false;
static int silence_counter = 0;
// const char *file_path = "vad_record.pcm";
SemaphoreHandle_t recording_done_semaphore = NULL;
afe_fetch_result_t *afe_fetch_result = NULL;

extern BaseType_t xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,
                                             const char * const pcName,
                                             const uint32_t usStackDepth,
                                             void * const pvParameters,
                                             UBaseType_t uxPriority,
                                             TaskHandle_t * const pxCreatedTask,
                                             const BaseType_t xCoreID );


void vad_record()
{
}


void feed_Task(void *arg)
{
    // char *audio_sr_input_fmt = AUDIO_ADC_INPUT_CH_FORMAT;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);
    if (NULL == pipeline) {
        return;
    }

    int bits_per_sample = CODEC_ADC_BITS_PER_SAMPLE;
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(CODEC_ADC_I2S_PORT, 16000, bits_per_sample, AUDIO_STREAM_READER);
    audio_element_handle_t i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    audio_element_handle_t filter = NULL;
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 16000;
    rsp_cfg.dest_rate = 16000;
    rsp_cfg.dest_ch = 1;
    filter = rsp_filter_init(&rsp_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t raw_read = raw_stream_init(&raw_cfg);

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

    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int feed_channel = 1;
    assert(nch == feed_channel);
    size_t len = audio_chunksize * sizeof(int16_t) * feed_channel;
    int16_t *buffer = heap_caps_malloc(len,MALLOC_CAP_SPIRAM);
    assert(buffer != NULL);

    while (task_flag) {
        if(sr_stop_flag){
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // bsp_codec_dev_open(16000, 1, 16, CODEC_INPUT);
        // bsp_codec_dev_read(true, (int8_t *)buffer, len);
        raw_stream_read(raw_read, (char *)buffer, len);
        afe_handle->feed(afe_data, buffer);
    }

    heap_caps_free(buffer);
    // bsp_codec_dev_close(CODEC_INPUT);
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, i2s_stream_reader);
    audio_pipeline_unregister(pipeline, filter);
    audio_pipeline_unregister(pipeline, raw_read);

    audio_pipeline_deinit(pipeline);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(filter);
    audio_element_deinit(raw_read);

    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    printf("multinet:%s\n", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    printf("RAM left %lu\n", esp_get_free_heap_size());
    model_iface_data_t *model_data = multinet->create(mn_name, timeout); //这一步为加载模型数据，psram <= 2mb会不够用
    esp_mn_commands_update_from_sdkconfig(multinet, model_data); // Add speech commands from sdkconfig
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

    esp_mn_commands_clear();
    esp_mn_commands_update();
    //print active speech commands
//    multinet->print_active_speech_commands(model_data);

    printf("------------detect start------------\n");

    while (task_flag) {
        if(sr_stop_flag){
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        afe_fetch_result = afe_handle->fetch(afe_data);
        if (!afe_fetch_result || afe_fetch_result->ret_value == ESP_FAIL) {
            printf("fetch error!\n");
            continue;
        }

        // if (vad_record_flag){
        //     vad_record();
        //     continue;
        // }

        if (afe_fetch_result->wakeup_state == WAKENET_DETECTED) {
            printf("WAKEWORD DETECTED\n");
	        multinet->clean(model_data);

	        if(wakeup_word[0] != '\0'){
	            tts_generate(wakeup_word);
	        }

        }

        if (afe_fetch_result->raw_data_channels == 1 && afe_fetch_result->wakeup_state == WAKENET_DETECTED) {
            wakeup_flag = 1;
        } else if (afe_fetch_result->raw_data_channels > 1 && afe_fetch_result->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            // For a multi-channel AFE, it is necessary to wait for the channel to be verified.
            printf("AFE_FETCH_CHANNEL_VERIFIED, channel index: %d\n", afe_fetch_result->trigger_channel_id);
            wakeup_flag = 1;
        }

        if (wakeup_flag == 1) {
            esp_mn_state_t mn_state = multinet->detect(model_data, afe_fetch_result->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                for (int i = 0; i < mn_result->num; i++) {
                    printf("TOP %d, command_id: %d, phrase_id: %d, string:%s prob: %f\n",
                    i+1, mn_result->command_id[i], mn_result->phrase_id[i], mn_result->string, mn_result->prob[i]);
                }
                if (mn_result->num > 0) {
                    latest_command_id = mn_result->command_id[0];
                    afe_handle->enable_wakenet(afe_data);
                    wakeup_flag=0;
                    printf("\n-----------awaits to be waken up-----------\n");
                    continue;
                }
//                printf("\n-----------listening-----------\n");
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("timeout, string:%s\n", mn_result->string);
                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;
                printf("\n-----------awaits to be waken up-----------\n");
                continue;
            }
        }
    }
    if (model_data) {
        multinet->destroy(model_data);
        model_data = NULL;
    }

    task_flag = 0;
    printf("detect exit\n");
    vTaskDelete(NULL);
}

void sr_init(const char *word, uint16_t t)
{
    if (word && word[0] != '\0') {
        strncpy(wakeup_word, word, sizeof(wakeup_word) - 1);
        wakeup_word[sizeof(wakeup_word) - 1] = '\0';  // 确保字符串以 '\0' 结尾
    }
    timeout = t;

    // bsp_codec_dev_open(16000, 1, 16, CODEC_INPUT);
    models = esp_srmodel_init("sr_module");

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
//    afe_config_print(afe_config);
    afe_handle = esp_afe_handle_from_config(afe_config);

    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
//    afe_handle->disable_vad(afe_data);
    afe_handle->disable_aec(afe_data);
    afe_config_free(afe_config);

    task_flag = 1;
    
    xTaskCreatePinnedToCore(feed_Task, "feed", 4 * 1024, (void*)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_Task, "detect", 4 * 1024, (void*)afe_data, 5, NULL, 1);
}

int get_latest_command_id(void) {
    return latest_command_id;
}

int get_wakeup_flag(void) {
    return wakeup_flag;
}

void reset_latest_command_id(void) {
    latest_command_id = 0;
}
void start_vad_record(void) {
    // recording_done_semaphore = xSemaphoreCreateBinary();
    // // 调用函数后，将录音标志置为 true
    // vad_record_flag = true;

    // xSemaphoreTake(recording_done_semaphore, portMAX_DELAY);
    // vSemaphoreDelete(recording_done_semaphore);
}
