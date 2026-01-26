/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_device.h"
#include "dev_audio_codec.h"
#include "dev_display_lcd.h"

// Device configuration structures
const static dev_audio_codec_config_t esp_bmgr_audio_dac_cfg = {
    .name = "audio_dac",
    .chip = "es8388",
    .type = "audio_codec",
    .adc_enabled = false,
    .adc_max_channel = 0,
    .adc_channel_mask = 0x3,
    .adc_channel_labels = "",
    .adc_init_gain = 0,
    .dac_enabled = true,
    .dac_max_channel = 2,
    .dac_channel_mask = 0x3,
    .dac_init_gain = 0,
    .pa_cfg = {
            .name = "none",
            .port = -1,
            .active_level = 0,
            .gain = 0.0,
        },
    .i2c_cfg = {
            .name = "i2c_master",
            .port = 0,
            .address = 32,
            .frequency = 400000,
        },
    .i2s_cfg = {
            .name = "i2s_audio_out",
            .port = 0,
            .clk_src = 0,
        },
    .metadata = NULL,
    .metadata_size = 0,
    .mclk_enabled = false,
    .aec_enabled = false,
    .eq_enabled = false,
    .alc_enabled = false,
};

const static dev_audio_codec_config_t esp_bmgr_audio_adc_cfg = {
    .name = "audio_adc",
    .chip = "es8388",
    .type = "audio_codec",
    .adc_enabled = true,
    .adc_max_channel = 2,
    .adc_channel_mask = 0x3,
    .adc_channel_labels = "",
    .adc_init_gain = 0,
    .dac_enabled = false,
    .dac_max_channel = 0,
    .dac_channel_mask = 0x0,
    .dac_init_gain = 0,
    .pa_cfg = {
            .name = "none",
            .port = -1,
            .active_level = 0,
            .gain = 0.0,
        },
    .i2c_cfg = {
            .name = "i2c_master",
            .port = 0,
            .address = 32,
            .frequency = 400000,
        },
    .i2s_cfg = {
            .name = "i2s_audio_in",
            .port = 0,
            .clk_src = 0,
        },
    .metadata = NULL,
    .metadata_size = 0,
    .mclk_enabled = false,
    .aec_enabled = false,
    .eq_enabled = false,
    .alc_enabled = false,
};

const static dev_display_lcd_config_t esp_bmgr_display_lcd_cfg = {
    .name = "display_lcd",
    .chip = "jd9853",
    .sub_type = "spi",
    .lcd_width = 320,
    .lcd_height = 172,
    .swap_xy = false,
    .mirror_x = true,
    .mirror_y = true,
    .need_reset = true,
    .invert_color = false,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    .bits_per_pixel = 16,
    .sub_cfg = {
            .spi = {
                    .spi_name = "spi_display",
                    .panel_config = {
                            .reset_gpio_num = 34,
                            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
                            .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
                            .bits_per_pixel = 16,
                            .flags = {
                                    .reset_active_high = false,
                                },
                            .vendor_config = NULL,
                        },
                    .io_spi_config = {
                            .cs_gpio_num = 34,
                            .dc_gpio_num = 35,
                            .spi_mode = 0,
                            .pclk_hz = 60000000,
                            .trans_queue_depth = 1,
                            .lcd_cmd_bits = 8,
                            .lcd_param_bits = 8,
                            .cs_ena_pretrans = 0,
                            .cs_ena_posttrans = 0,
                            .flags = {
                                    .dc_high_on_cmd = false,
                                    .dc_low_on_data = false,
                                    .dc_low_on_param = false,
                                    .octal_mode = false,
                                    .quad_mode = false,
                                    .sio_mode = false,
                                    .lsb_first = false,
                                    .cs_high_active = false,
                                },
                        },
                },
        },
};

// Device descriptor array
const esp_board_device_desc_t g_esp_board_devices[] = {
    {
        .next = &g_esp_board_devices[1],
        .name = "audio_dac",
        .type = "audio_codec",
        .cfg = &esp_bmgr_audio_dac_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_dac_cfg),
        .init_skip = false,
    },
    {
        .next = &g_esp_board_devices[2],
        .name = "audio_adc",
        .type = "audio_codec",
        .cfg = &esp_bmgr_audio_adc_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_adc_cfg),
        .init_skip = false,
    },
    {
        .next = NULL,
        .name = "display_lcd",
        .type = "display_lcd",
        .cfg = &esp_bmgr_display_lcd_cfg,
        .cfg_size = sizeof(esp_bmgr_display_lcd_cfg),
        .init_skip = false,
    },
};
