/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "hal/gpio_types.h"
#include "periph_i2s.h"
#include "periph_spi.h"

// Peripheral configuration structures
const static i2c_master_bus_config_t esp_bmgr_i2c_master_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 44,
    .scl_io_num = 43,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .intr_priority = 1,
    .trans_queue_depth = 0,
    .flags = {
            .enable_internal_pullup = true,
        },
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_out_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_STD,
    .direction = I2S_DIR_TX,
    .i2s_cfg = {
            .std = {
                    .clk_cfg = {
                            .sample_rate_hz = 16000,
                            .clk_src = I2S_CLK_SRC_DEFAULT,
                            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                            .ext_clk_freq_hz = 0,
                        },
                    .slot_cfg = {
                            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                            .slot_mode = I2S_SLOT_MODE_STEREO,
                            .slot_mask = I2S_STD_SLOT_BOTH,
                            .ws_width = 16,
                            .ws_pol = false,
                            .bit_shift = true,
                            .left_align = true,
                            .big_endian = false,
                            .bit_order_lsb = false,
                        },
                    .gpio_cfg = {
                            .mclk = 39,
                            .bclk = 41,
                            .ws = 42,
                            .dout = 38,
                            .din = 40,
                            .invert_flags = {
                                    .mclk_inv = false,
                                    .bclk_inv = false,
                                    .ws_inv = false,
                                },
                        },
                },
        },
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_in_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_STD,
    .direction = I2S_DIR_RX,
    .i2s_cfg = {
            .std = {
                    .clk_cfg = {
                            .sample_rate_hz = 48000,
                            .clk_src = I2S_CLK_SRC_DEFAULT,
                            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                            .ext_clk_freq_hz = 0,
                        },
                    .slot_cfg = {
                            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                            .slot_mode = I2S_SLOT_MODE_STEREO,
                            .slot_mask = I2S_STD_SLOT_BOTH,
                            .ws_width = 16,
                            .ws_pol = false,
                            .bit_shift = true,
                            .left_align = true,
                            .big_endian = false,
                            .bit_order_lsb = false,
                        },
                    .gpio_cfg = {
                            .mclk = -1,
                            .bclk = -1,
                            .ws = -1,
                            .dout = -1,
                            .din = -1,
                            .invert_flags = {
                                    .mclk_inv = false,
                                    .bclk_inv = false,
                                    .ws_inv = false,
                                },
                        },
                },
        },
};

static periph_spi_config_t esp_bmgr_spi_display_cfg = {
    .spi_port = SPI2_HOST,
    .spi_bus_config = {
            .mosi_io_num = 37,
            .miso_io_num = -1,
            .sclk_io_num = 36,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .data4_io_num = -1,
            .data5_io_num = -1,
            .data6_io_num = -1,
            .data7_io_num = -1,
            .data_io_default_level = false,
            .max_transfer_sz = 110080,
            .flags = 0,
            .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
            .intr_flags = 0,
        },
};

// Peripheral descriptor array
const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {
        .next = &g_esp_board_peripherals[1],
        .name = "i2c_master",
        .type = "i2c",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2c_master_cfg,
        .cfg_size = sizeof(esp_bmgr_i2c_master_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[2],
        .name = "i2s_audio_out",
        .type = "i2s",
        .format = "std-out",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_out_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_out_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[3],
        .name = "i2s_audio_in",
        .type = "i2s",
        .format = "std-in",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_in_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_in_cfg),
        .id = 0,
    },
    {
        .next = NULL,
        .name = "spi_display",
        .type = "spi",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_spi_display_cfg,
        .cfg_size = sizeof(esp_bmgr_spi_display_cfg),
        .id = 0,
    },
};
