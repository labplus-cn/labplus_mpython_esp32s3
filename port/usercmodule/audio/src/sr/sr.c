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
static esp_afe_sr_data_t *s_afe_data = NULL;
static volatile bool wakenet_enabled = true;
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
}

//AFE支持两路MIC或一路mic输入，但单路数据少，处理更快，所以用通道转换，把两路mic输入转一路送AFE
void feed_Task(void *arg)
{
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    assert(codec_dev != NULL);
    esp_codec_dev_set_in_gain(codec_dev, 35.0);
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data); //返回每通道采样点数
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int i2s_buff_len = audio_chunksize * sizeof(int16_t) * nch * 2; //使用通道转换，因为两路mic输入，数据量增大一倍，再乘2
    // int i2s_buff_len = audio_chunksize * sizeof(int16_t) * nch; 
    ESP_LOGI(TAG, "buff len: %d", i2s_buff_len);
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
        esp_codec_dev_read(codec_dev, (void *)i2s_buff, i2s_buff_len);
        esp_ae_ch_cvt_process(c_handle, audio_chunksize, (esp_ae_sample_t)i2s_buff, (esp_ae_sample_t)afe_buff);
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
#if defined(MPYTHON_LANGUAGE_EN) && MPYTHON_LANGUAGE_EN
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
#else
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
#endif
    // ESP_LOGI(TAG, "multinet:%s", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    // ESP_LOGI(TAG, "RAM left %lu", esp_get_free_heap_size());
    model_iface_data_t *model_data = multinet->create(mn_name, timeout); //这一步为加载模型数据，psram <= 2mb会不够用
    // esp_mn_commands_update_from_sdkconfig(multinet, model_data); // 不从sdkconfig加载，由Python通过sr.add()+sr.update()设置
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);
    multinet_ready = true;  // 通知sr.update()可以安全调用esp_mn_commands_update()
    wakenet_enabled = true;

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

        // 仅在当前处于未唤醒状态时检测唤醒词
        if (wakeup_flag == 0) {
            if (afe_result->raw_data_channels == 1 && afe_result->wakeup_state == WAKENET_DETECTED) {
                ESP_LOGI(TAG, "WAKEWORD DETECTED");
                multinet->clean(model_data);
#if !(defined(MPYTHON_LANGUAGE_EN) && MPYTHON_LANGUAGE_EN)
                if (wakeup_word[0] != '\0') {
                    text_to_speech(wakeup_word);
                }
#endif
                afe_handle->disable_wakenet(afe_data);
                wakenet_enabled = false;
                wakeup_flag = 1;
            } else if (afe_result->raw_data_channels > 1 && afe_result->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
                ESP_LOGI(TAG, "AFE_FETCH_CHANNEL_VERIFIED, channel index: %d", afe_result->trigger_channel_id);
                multinet->clean(model_data);
#if !(defined(MPYTHON_LANGUAGE_EN) && MPYTHON_LANGUAGE_EN)
                if (wakeup_word[0] != '\0') {
                    text_to_speech(wakeup_word);
                }
#endif
                afe_handle->disable_wakenet(afe_data);
                wakenet_enabled = false;
                wakeup_flag = 1;
            }
        }

        if (wakeup_flag == 1) {
            // 确保在命令词识别阶段禁用唤醒词，释放CPU并防止误触发reset
            if (wakenet_enabled) {
                afe_handle->disable_wakenet(afe_data);
                wakenet_enabled = false;
                ESP_LOGI(TAG, "[AFE] WakeNet disabled, listening for command... (timeout=%d ms)", timeout);
            }

            esp_mn_state_t mn_state = multinet->detect(model_data, afe_result->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                if (mn_result) {
                    ESP_LOGI(TAG, "[MN_DETECTED] Candidates count: %d", mn_result->num);
                    for (int i = 0; i < mn_result->num; i++) {
                        ESP_LOGI(TAG, "  -> #%d: cmd_id=%d, phrase_id=%d, prob=%.4f, str='%s'",
                        i+1, mn_result->command_id[i], mn_result->phrase_id[i], mn_result->prob[i], mn_result->string);
                    }
                    if (mn_result->num > 0) {
                        latest_command_id = mn_result->command_id[0];
                    }
                } else {
                    ESP_LOGI(TAG, "[MN_DETECTED] No result struct returned");
                }
                // 无论是否匹配到有效命令词，检测结束都必须恢复wakenet并清理multinet，避免卡死
                afe_handle->enable_wakenet(afe_data);
                wakenet_enabled = true;
                wakeup_flag = 0;
                multinet->clean(model_data);
                ESP_LOGI(TAG, "-----------awaits to be waken up (WakeNet re-enabled)-----------");
                continue;
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                ESP_LOGI(TAG, "[MN_TIMEOUT] Timeout reached! partial string='%s'", (mn_result && mn_result->string) ? mn_result->string : "");
                afe_handle->enable_wakenet(afe_data);
                wakenet_enabled = true;
                wakeup_flag = 0;
                multinet->clean(model_data);
                ESP_LOGI(TAG, "-----------awaits to be waken up (WakeNet re-enabled)-----------");
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

void sr_init(const char *word, uint16_t t)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("AFE_SR", ESP_LOG_INFO);
    esp_log_level_set("AFE", ESP_LOG_INFO);
    esp_log_level_set("ESP_MN", ESP_LOG_INFO);
    esp_log_level_set("MULTINET", ESP_LOG_INFO);

    if (word && word[0] != '\0') {
        strncpy(wakeup_word, word, sizeof(wakeup_word) - 1);
        wakeup_word[sizeof(wakeup_word) - 1] = '\0';  // 确保字符串以 '\0' 结尾
    }
    timeout = t;

    ESP_LOGI(TAG, "sr_init: timeout=%d, wakeup_word='%s'", timeout, wakeup_word);

    // bsp_codec_dev_open(16000, 1, 16, CODEC_INPUT);
    models = esp_srmodel_init("model");

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_config->afe_ringbuf_size = 50;  // 增大ring buffer，避免multinet推理慢导致feed buffer溢出
    afe_handle = esp_afe_handle_from_config(afe_config);

    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_handle->disable_aec(afe_data);
    afe_config_free(afe_config);

#if !(defined(MPYTHON_LANGUAGE_EN) && MPYTHON_LANGUAGE_EN)
    if (word && word[0] != '\0') {
        model_init();  // 提前初始化TTS，避免首次唤醒时阻塞detect_Task导致AFE ring buffer溢出
    }
#endif

    s_afe_data = afe_data;
    wakenet_enabled = true;
    task_flag = 1;

    xTaskCreatePinnedToCore(detect_Task, "detect", 8 * 1024, (void*)afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(feed_Task, "feed", 4 * 1024, (void*)afe_data, 5, NULL, 0);
    ESP_LOGI(TAG, "sr_init: tasks created successfully");
}
int get_latest_command_id(void) {
    return latest_command_id;
}
int get_wakeup_flag(void) {
    return wakeup_flag;
}

void set_wakeup_flag(void) {
    if(wakeup_flag == 0){
#if !(defined(MPYTHON_LANGUAGE_EN) && MPYTHON_LANGUAGE_EN)
        if(wakeup_word[0] != '\0'){
            text_to_speech(wakeup_word);
        }
#endif
        if (afe_handle && s_afe_data) {
            afe_handle->disable_wakenet(s_afe_data);
            wakenet_enabled = false;
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
