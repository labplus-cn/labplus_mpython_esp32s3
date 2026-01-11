/**
 *
 * @copyright Copyright 2021 Espressif Systems (Shanghai) Co. Ltd.
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *               http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "string.h"
#include "es8388_codec.h"
#include "bsp_audio_board.h"
#include <stdint.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "soc/soc_caps.h"
#else
#include "driver/i2s.h"
#endif
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_rom_sys.h"
#include "esp_check.h"
// #include "audio/fatfs/audio_file.h"
#if ((SOC_SDMMC_HOST_SUPPORTED) && (FUNC_SDMMC_EN))
#include "driver/sdmmc_host.h"
#endif /* ((SOC_SDMMC_HOST_SUPPORTED) && (FUNC_SDMMC_EN)) */

#define GPIO_MUTE_NUM   GPIO_NUM_1
#define GPIO_MUTE_LEVEL 1
#define ACK_CHECK_EN   0x1     /*!< I2C master will check ack from slave*/
#define ADC_I2S_CHANNEL 4
static const char *TAG = "board";
#define I2S_NUM I2S_NUM_1

static bsp_codec_dev_t *codec_dev = NULL;

static esp_err_t _bsp_i2s_init(uint32_t sample_rate, int channel_format, int bits_per_sample)
{
    esp_err_t ret_val = ESP_OK;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ret_val |= i2s_new_channel(&chan_cfg,  &(codec_dev->tx_handle), &(codec_dev->rx_handle));
    // i2s_std_config_t std_cfg = I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_sample);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_I2S_MCLK,
            .bclk = GPIO_I2S_SCLK,
            .ws = GPIO_I2S_LRCK,
            .dout = GPIO_I2S_DOUT,
            .din = GPIO_I2S_SDIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = 256;

    // ESP_LOGE(TAG, "i2s std config: samperate:%d channels: %d bits: %d", sample_rate,  channel_fmt, bits_per_sample);
    ret_val |= i2s_channel_init_std_mode(codec_dev->tx_handle, &std_cfg);
    ret_val |= i2s_channel_init_std_mode(codec_dev->rx_handle, &std_cfg);
    ret_val |= i2s_channel_enable(codec_dev->tx_handle);
    ret_val |= i2s_channel_enable(codec_dev->rx_handle);
#endif

    return ret_val;
}

/* 删除i2s通道，并释放相关资源。*/
static esp_err_t _bsp_i2s_deinit(void)
{
    esp_err_t ret_val = ESP_OK;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    if (codec_dev->rx_handle) {
        ret_val |= i2s_channel_disable(codec_dev->rx_handle);
        ret_val |= i2s_del_channel(codec_dev->rx_handle);
        codec_dev->rx_handle = NULL;
    }
    if (codec_dev->tx_handle) {
        ret_val |= i2s_channel_disable(codec_dev->tx_handle);
        ret_val |= i2s_del_channel(codec_dev->tx_handle);
        codec_dev->tx_handle = NULL;
    }
#else
    ret_val |= i2s_stop(I2S_NUM);
    ret_val |= i2s_driver_uninstall(I2S_NUM);
#endif

    return ret_val;
}

esp_err_t _bsp_i2c_init(i2c_port_t i2c_num, uint32_t clk_speed)
{
    // static i2c_config_t i2c_cfg = {
    //     .mode = I2C_MODE_MASTER,
    //     .scl_io_num = GPIO_I2C_SCL,
    //     .sda_io_num = GPIO_I2C_SDA,
    //     .scl_pullup_en = GPIO_PULLUP_DISABLE,
    //     .sda_pullup_en = GPIO_PULLUP_DISABLE,
    //     .master.clk_speed = 400000,
    //     // .clk_flags = I2C_SCLK_SRC_FLAG_AWARE_DFS,
    // };
    // esp_err_t ret = i2c_param_config(i2c_num, &i2c_cfg);
    // if (ret != ESP_OK) {
    //     return ESP_FAIL;
    // }
    // return i2c_driver_install(i2c_num, i2c_cfg.mode, 0, 0, 0);
    return ESP_OK;
}

esp_err_t bsp_codec_dev_create(void)
{
    esp_err_t ret_val = ESP_OK;

    if(codec_dev){
        return ret_val;
    }
    codec_dev = (bsp_codec_dev_t *)heap_caps_malloc(sizeof(bsp_codec_dev_t) ,MALLOC_CAP_SPIRAM);
    assert(codec_dev != NULL);
    codec_dev->semaphore_codec_dev_input = xSemaphoreCreateRecursiveMutex();
    assert(codec_dev->semaphore_codec_dev_input != NULL);
    codec_dev->semaphore_codec_dev_output = xSemaphoreCreateRecursiveMutex();
    assert(codec_dev->semaphore_codec_dev_output != NULL);

    _bsp_i2s_init(DEFAULT_SAMPLE_RATE, DEFAULT_CHANNEL_FORMT, DEFAULT_BITS_PER_SAMPLE);

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .rx_handle = codec_dev->rx_handle,
        .tx_handle = codec_dev->tx_handle,
#endif
    };
    codec_dev->codec_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    assert(codec_dev->codec_data_if != NULL);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8388_CODEC_DEFAULT_ADDR};
    codec_dev->codec_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(codec_dev->codec_ctrl_if != NULL);
    // New input codec interface
    es8388_codec_cfg_t es8388_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .ctrl_if = codec_dev->codec_ctrl_if,
        .master_mode = false,
    };
    codec_dev->codec_if = es8388_codec_new(&es8388_cfg);
    assert(codec_dev->codec_if != NULL);

    esp_codec_dev_cfg_t outdev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_dev->codec_if,
        .data_if = codec_dev->codec_data_if,
    };
    codec_dev->output_codec_dev = esp_codec_dev_new(&outdev_cfg);
    assert(codec_dev->output_codec_dev != NULL);
    esp_codec_dev_set_out_vol(codec_dev->output_codec_dev, PLAYER_VOLUME); 

    // New input codec device
    esp_codec_dev_cfg_t indev_cfg = {
        .codec_if = codec_dev->codec_if,
        .data_if = codec_dev->codec_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    codec_dev->input_codec_dev = esp_codec_dev_new(&indev_cfg);
    assert(codec_dev->input_codec_dev != NULL);

    esp_codec_set_disable_when_closed(codec_dev->output_codec_dev, false);
    esp_codec_set_disable_when_closed(codec_dev->input_codec_dev, false);    
    
    return ret_val;
}

esp_err_t bsp_codec_dev_delete(void)
{
    esp_err_t ret_val = ESP_OK;

    assert(codec_dev != NULL);
    _bsp_i2s_deinit();

    if (codec_dev->input_codec_dev) {
        esp_codec_dev_close(codec_dev->input_codec_dev);
        esp_codec_dev_delete(codec_dev->input_codec_dev);
        codec_dev->input_codec_dev = NULL;
    }

    if (codec_dev->output_codec_dev) {
        esp_codec_dev_close(codec_dev->output_codec_dev);
        esp_codec_dev_delete(codec_dev->output_codec_dev);
        codec_dev->output_codec_dev = NULL;
    }
    // Delete codec interface
    if (codec_dev->codec_if) {
        audio_codec_delete_codec_if(codec_dev->codec_if);
        codec_dev->codec_if = NULL;
    }
    
    // Delete codec control interface
    if (codec_dev->codec_ctrl_if) {
        audio_codec_delete_ctrl_if(codec_dev->codec_ctrl_if);
        codec_dev->codec_ctrl_if = NULL;
    }
    
    // Delete codec data interface
    if (codec_dev->codec_data_if) {
        audio_codec_delete_data_if(codec_dev->codec_data_if);
        codec_dev->codec_data_if = NULL;
    }
    heap_caps_free(codec_dev);
    return ret_val;
}

esp_err_t bsp_codec_dev_open(uint32_t sample_rate, int channel_format, int bits_per_sample, codec_type_t type)
{
    esp_err_t err = ESP_OK;
    
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channel_format,
        .bits_per_sample = bits_per_sample,
    };

    if(type == CODEC_INPUT){
        assert(codec_dev->input_codec_dev != NULL);
        if (xSemaphoreTakeRecursive(codec_dev->semaphore_codec_dev_input, pdMS_TO_TICKS(10000)) != pdTRUE) {
                ESP_LOGE(TAG, "Open input_codec_dev fail.");
                return ESP_FAIL;
        }
        err = esp_codec_dev_open(codec_dev->input_codec_dev, &fs);
        if(err == ESP_OK){
            codec_dev->is_codec_dev_input_open = true;
        }
        esp_codec_dev_set_in_gain(codec_dev->input_codec_dev, RECORD_VOLUME); 
    }else if(type == CODEC_OUTPUT){
         assert(codec_dev->output_codec_dev != NULL);
        if (xSemaphoreTakeRecursive(codec_dev->semaphore_codec_dev_output, pdMS_TO_TICKS(10000)) != pdTRUE) {
                ESP_LOGE(TAG, "Open output_codec_dev fail.");
                return ESP_FAIL;
        }
        err = esp_codec_dev_open(codec_dev->output_codec_dev, &fs);
        if(err == ESP_OK){
            codec_dev->is_codec_dev_output_open = true;
        }
    }
 
    return err;
}

esp_err_t bsp_codec_dev_close(codec_type_t type)
{
    if(type == CODEC_INPUT){
        xSemaphoreGiveRecursive(codec_dev->semaphore_codec_dev_input);
        if(codec_dev->is_codec_dev_input_open){
            esp_codec_dev_close(codec_dev->input_codec_dev);
            codec_dev->is_codec_dev_input_open = false;
        }
    }else if(type == CODEC_OUTPUT){
        xSemaphoreGiveRecursive(codec_dev->semaphore_codec_dev_output);
        if(codec_dev->is_codec_dev_output_open){
            esp_codec_dev_close(codec_dev->output_codec_dev);
            codec_dev->is_codec_dev_output_open = false;
        }
    }

    return ESP_OK;
}

esp_err_t bsp_codec_dev_set_out_vol(int volume)
{
    assert(codec_dev->output_codec_dev != NULL);
    esp_codec_dev_set_out_vol(codec_dev->output_codec_dev, volume);
    return ESP_OK;
}

esp_err_t bsp_codec_dev_get_out_vol(int *volume)
{
    assert(codec_dev->output_codec_dev != NULL);
    esp_codec_dev_get_out_vol(codec_dev->output_codec_dev, volume);
    return ESP_OK;
}

esp_err_t bsp_codec_dev_write(const int8_t  *data, int length, TickType_t ticks_to_wait)
{
    esp_err_t ret = ESP_OK;

    assert(codec_dev->output_codec_dev != NULL);
    if(codec_dev->is_codec_dev_output_open){
        ret = esp_codec_dev_write(codec_dev->output_codec_dev, (void *)data, length);
    }
    
    return ret;
}

/* afe    is_get_raw_channel = true :16K 16bit 左声道 + 右声道，无ref, 
          is_get_raw_channel = false :16K 16bit 左声道 + 右声道 + 一路ref,*/
esp_err_t bsp_codec_dev_read(bool is_get_raw_channel, int8_t *buffer, int buffer_len)
{
    esp_err_t ret = ESP_OK;
    int audio_chunksize;

    assert(codec_dev->input_codec_dev != NULL);
    if(codec_dev->is_codec_dev_input_open){
        ret = esp_codec_dev_read(codec_dev->input_codec_dev, (void *)buffer, buffer_len);
    }

    if (!is_get_raw_channel) {
        audio_chunksize = buffer_len / (sizeof(int16_t) * ADC_I2S_CHANNEL);  //左
        for (int i = 0; i < audio_chunksize; i++) {
            int16_t ref = buffer[4 * i + 0];
            buffer[2 * i + 0] = buffer[4 * i + 1];
            buffer[2 * i + 1] = ref;
        }
    }

    return ret;
}

bsp_codec_dev_t* get_codec_dev(void)
{
    return codec_dev;
}


