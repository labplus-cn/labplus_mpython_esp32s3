#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_process_sdkconfig.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"
#include "esp_afe_config.h"
#include "esp_mn_speech_commands.h"
// #include "sr_module.h"
#include "tts.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_ae_ch_cvt.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SPEECH";

int wakeup_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
//static esp_afe_sr_data_t *afe_data = NULL;
static volatile int task_flag = 0;
volatile int latest_command_id = 0;
srmodel_list_t *models = NULL;
static char wakeup_word[64] = "";
static uint16_t timeout  = 6000;
static bool vad_record_flag = false;
static volatile bool multinet_ready = false;  // detect_Task完成multinet初始化后置true
// const char *file_path = "vad_record.pcm";
SemaphoreHandle_t recording_done_semaphore = NULL;
afe_fetch_result_t *afe_result = NULL;

void vad_record()
{
    // if (afe_result->vad_state == VAD_SPEECH) {
    //     if (!recording) {
    //         silence_counter = 0;
    //         // 第一次检测到语音，开始录音
    //         wav_codec = (wav_codec_t*) heap_caps_malloc(sizeof(wav_codec_t), MALLOC_CAP_SPIRAM);
    //         wav_codec->lfs2_file = vfs_lfs2_file_open(file_path, LFS2_O_WRONLY | LFS2_O_CREAT | LFS2_O_TRUNC);
    //         recording = true;
    //         printf("检测到语音，开始录音\n");
    //     }
    // }else {  // VAD_SILENCE
    //     if (recording) {
    //         silence_counter++;
    //         if (silence_counter >= SILENCE_THRESHOLD) {
    //             recording = false;
    //             vad_record_flag=false;
    //             silence_counter = 0;
    //             if (wav_codec) {
    //                 vfs_lfs2_file_close(wav_codec->lfs2_file);
    //                 if(wav_codec){
    //                     heap_caps_free(wav_codec);
    //                     wav_codec=NULL;
    //                 }
    //             }
    //             printf("检测到静音，停止录音\n");
    //             // 录音结束，释放信号量
    //             xSemaphoreGive(recording_done_semaphore);
    //         }
    //     }
    // }
    // if (recording && wav_codec) {
    //     if (afe_result->vad_cache_size > 0) {
    //         printf("写入 vad cache: %d 字节\n",afe_result->vad_cache_size);
    //         lfs2_file_write(&wav_codec->lfs2_file->vfs->lfs,
    //                         &wav_codec->lfs2_file->file,
    //                         afe_result->vad_cache,
    //                         afe_result->vad_cache_size);
    //     }
    //     if (afe_result->vad_state == 1) {
    //         lfs2_file_write(&wav_codec->lfs2_file->vfs->lfs,
    //                         &wav_codec->lfs2_file->file,
    //                         afe_result->data,
    //                         afe_result->data_size);
    //     }
    // }
}

//AFE支持两路MIC或一路mic输入，但单路数据少，处理更快，所以用通道转换，把两路mic输入转一路送AFE
//如果使用双mic送AFE,注释掉所有通道转换相关代码。这种方式，似乎配置成"MM"有问题，需要配置成"M",然后
//能完配置afe_config->pcm_config相关参数为双通道才行。
//sr-bak2.c使用esp-gmf pipe实现，但命令词识别响应较慢，找不到原因，暂时用本代码实现。
void feed_Task(void *arg)
{
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    assert(codec_dev != NULL);
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data); //返回每通道采样点数
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int i2s_buff_len = audio_chunksize * sizeof(int16_t) * nch * 2; //使用通道转换，因为两路mic输入，数据量增大一倍，再乘2
    // int i2s_buff_len = audio_chunksize * sizeof(int16_t) * nch; 
    ESP_LOGI("TAG", "buff len: %d", i2s_buff_len);
    int16_t *i2s_buff = heap_caps_malloc(i2s_buff_len,MALLOC_CAP_SPIRAM);
    assert(i2s_buff != NULL);
    int16_t *afe_buff = heap_caps_malloc(i2s_buff_len/2,MALLOC_CAP_SPIRAM);
    assert(afe_buff != NULL);

    esp_ae_ch_cvt_cfg_t config;
    float w_data[2] = {1.0, 1.0};
    config.sample_rate = 16000;
    config.src_ch = 2;
    config.dest_ch = 1;
    config.weight = w_data;
    config.weight_len = 2;
    config.bits_per_sample = 16;
    void *c_handle = NULL;
    int ret = esp_ae_ch_cvt_open(&config, &c_handle);
    TEST_ASSERT_NOT_EQUAL(c_handle, NULL);

    while (task_flag) {
        // int64_t us = esp_timer_get_time();
        esp_codec_dev_read(codec_dev, (void *)i2s_buff, i2s_buff_len);
        // ESP_LOGE("TAG", "time: %lld", esp_timer_get_time() - us);
        esp_ae_ch_cvt_process(c_handle, audio_chunksize, (esp_ae_sample_t)i2s_buff, (esp_ae_sample_t)afe_buff);
        // afe_handle->feed(afe_data, i2s_buff);
        afe_handle->feed(afe_data, afe_buff);
    }

    heap_caps_free(i2s_buff);
    heap_caps_free(afe_buff);
    esp_ae_ch_cvt_close(c_handle);
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    // ESP_LOGI(TAG, "multinet:%s", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    // ESP_LOGI(TAG, "RAM left %lu", esp_get_free_heap_size());
    model_iface_data_t *model_data = multinet->create(mn_name, timeout); //这一步为加载模型数据，psram <= 2mb会不够用
    // esp_mn_commands_update_from_sdkconfig(multinet, model_data); // 不从sdkconfig加载，由Python通过sr.add()+sr.update()设置
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);
    multinet_ready = true;  // 通知sr.update()可以安全调用esp_mn_commands_update()

    // esp_mn_commands_clear();
    // esp_mn_commands_update();
    //print active speech commands
//    multinet->print_active_speech_commands(model_data);

    // ESP_LOGI(TAG, "------------detect start------------");

    while (task_flag) {
        afe_result = afe_handle->fetch(afe_data);
        if (!afe_result || afe_result->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "fetch error!");
            break;
        }

        if (vad_record_flag){
            vad_record();
            continue;
        }

        if (afe_result->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "WAKEWORD DETECTED");
	        multinet->clean(model_data);

	        if(wakeup_word[0] != '\0'){
	            text_to_speech(wakeup_word);
	        }

        }

        if (afe_result->raw_data_channels == 1 && afe_result->wakeup_state == WAKENET_DETECTED) {
            wakeup_flag = 1;
        } else if (afe_result->raw_data_channels > 1 && afe_result->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            // For a multi-channel AFE, it is necessary to wait for the channel to be verified.
            ESP_LOGI(TAG, "AFE_FETCH_CHANNEL_VERIFIED, channel index: %d", afe_result->trigger_channel_id);
            wakeup_flag = 1;
        }

        if (wakeup_flag == 1) {
            esp_mn_state_t mn_state = multinet->detect(model_data, afe_result->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                for (int i = 0; i < mn_result->num; i++) {
                    ESP_LOGI(TAG, "TOP %d, command_id: %d, phrase_id: %d, string:%s prob: %f",
                    i+1, mn_result->command_id[i], mn_result->phrase_id[i], mn_result->string, mn_result->prob[i]);
                }
                if (mn_result->num > 0) {
                    latest_command_id = mn_result->command_id[0];
                    afe_handle->enable_wakenet(afe_data);
                    wakeup_flag=0;
                    ESP_LOGI(TAG, "-----------awaits to be waken up-----------");
                    continue;
                }
//                ESP_LOGI(TAG, "-----------listening-----------");
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                ESP_LOGI(TAG, "timeout, string:%s\n", mn_result->string);
                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;
                ESP_LOGI(TAG, "-----------awaits to be waken up-----------");
                continue;
            }
        }
    }
    if (model_data) {
        multinet->destroy(model_data);
        model_data = NULL;
    }
    ESP_LOGI(TAG, "detect exit.");
    vTaskDelete(NULL);
}

void sr_init(const char *word, uint16_t t, bool f)
{
    if (word && word[0] != '\0') {
        strncpy(wakeup_word, word, sizeof(wakeup_word) - 1);
        wakeup_word[sizeof(wakeup_word) - 1] = '\0';  // 确保字符串以 '\0' 结尾
    }
    timeout = t;

    // bsp_codec_dev_open(16000, 1, 16, CODEC_INPUT);
    models = esp_srmodel_init("model");

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    // afe_config->pcm_config.total_ch_num = 2;
    // afe_config->pcm_config.mic_num = 2;
    // afe_config->pcm_config.ref_num = 0;
    // afe_config->pcm_config.sample_rate = 16000;
    // afe_config_print(afe_config);
    afe_config->afe_ringbuf_size = 50;  // 增大ring buffer，避免multinet推理慢导致feed buffer溢出
    afe_handle = esp_afe_handle_from_config(afe_config);

    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
//    afe_handle->disable_vad(afe_data);
    afe_handle->disable_aec(afe_data);
    afe_config_free(afe_config);

    if (word && word[0] != '\0') {
        model_init();  // 提前初始化TTS，避免首次唤醒时阻塞detect_Task导致AFE ring buffer溢出
    }

    task_flag = 1;

    xTaskCreatePinnedToCore(detect_Task, "detect", 4 * 1024, (void*)afe_data, 10, NULL, 1);
    xTaskCreatePinnedToCore(feed_Task, "feed", 4 * 1024, (void*)afe_data, 5, NULL, 0);
}
int get_latest_command_id(void) {
    return latest_command_id;
}
int get_wakeup_flag(void) {
    return wakeup_flag;
}

void set_wakeup_flag(void) {
    if(wakeup_flag == 0){
        if(wakeup_word[0] != '\0'){
            text_to_speech(wakeup_word);
        }
        wakeup_flag = 1;
    }
}

void wait_for_multinet_ready(void) {
    while (!multinet_ready) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void reset_latest_command_id(void) {
    latest_command_id = 0;
}

// void start_vad_record(void) {
//     recording_done_semaphore = xSemaphoreCreateBinary();
//     // 调用函数后，将录音标志置为 true
//     vad_record_flag = true;

//     xSemaphoreTake(recording_done_semaphore, portMAX_DELAY);
//     vSemaphoreDelete(recording_done_semaphore);
// }
