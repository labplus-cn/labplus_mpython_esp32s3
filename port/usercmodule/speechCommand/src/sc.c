#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_process_sdkconfig.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "speech_commands_action.h"
#include "model_path.h"
#include "esp_afe_config.h"
#include "bsp_audio.h"
#include "esp_mn_speech_commands.h"
#include "sc_module.h"
#include "tts.h"

int wakeup_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
static volatile int task_flag = 0;
volatile int latest_command_id = 0;
srmodel_list_t *models = NULL;

static char wakeup_word[64] = "";
static uint16_t timeout  = 6000;
static bool enable_flag = false;

volatile int sc_stop_flag = 0;


void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int feed_channel = 1;
    assert(nch == feed_channel);
    int16_t *i2s_buff = heap_caps_malloc(audio_chunksize * sizeof(int16_t) * feed_channel,MALLOC_CAP_SPIRAM);
    assert(i2s_buff);

    while (task_flag) {
        if(sc_stop_flag){
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!dev_open_flag){
            bsp_codec_dev_open(16000, 1, 16);
        }

        bsp_get_feed_data(true, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);

        afe_handle->feed(afe_data, i2s_buff);
    }
    if (i2s_buff) {
        heap_caps_free(i2s_buff);
        i2s_buff = NULL;
    }
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

    //print active speech commands
    esp_mn_commands_clear();
    esp_mn_commands_update();
//    multinet->print_active_speech_commands(model_data);

    printf("------------detect start------------\n");
    while (task_flag) {
        if(sc_stop_flag){
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            printf("fetch error!\n");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            printf("WAKEWORD DETECTED\n");
	        multinet->clean(model_data);

	        if(wakeup_word[0] != '\0'){
                if(!get_tts_init_flag()){
                    model_init();
                }
	            text_to_speech(wakeup_word);
	        }

        }

        if (res->raw_data_channels == 1 && res->wakeup_state == WAKENET_DETECTED) {
            wakeup_flag = 1;
        } else if (res->raw_data_channels > 1 && res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            // For a multi-channel AFE, it is necessary to wait for the channel to be verified.
            printf("AFE_FETCH_CHANNEL_VERIFIED, channel index: %d\n", res->trigger_channel_id);
            wakeup_flag = 1;
        }

        if (wakeup_flag == 1) {
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

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
    printf("detect exit\n");
    vTaskDelete(NULL);
}

void sc_init(const char *word, uint16_t t, bool f)
{
    if (word && word[0] != '\0') {
        strncpy(wakeup_word, word, sizeof(wakeup_word) - 1);
        wakeup_word[sizeof(wakeup_word) - 1] = '\0';  // 确保字符串以 '\0' 结尾
    }
    timeout = t;
    enable_flag = f;

    bsp_codec_dev_open(16000, 1, 16);
    models = esp_srmodel_init("sr_module");

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
//    afe_config_print(afe_config);
    afe_handle = esp_afe_handle_from_config(afe_config);

    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_handle->disable_vad(afe_data);
    afe_handle->disable_aec(afe_data);
    afe_config_free(afe_config);

    task_flag = 1;

    xTaskCreatePinnedToCore(&detect_Task, "detect", 4 * 1024, (void*)afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(&feed_Task, "feed", 4 * 1024, (void*)afe_data, 5, NULL, 0);
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
