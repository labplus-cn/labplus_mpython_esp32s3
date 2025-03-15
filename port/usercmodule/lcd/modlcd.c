#include "lcd.h"

#include "py/runtime.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "logo_en_320x172_lcd.h"

static const char *TAG = "lcd";

lcd_t *lcd;

static bool on_color_trans_done_cb(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx
)
{
    lcd_t *lcd = (lcd_t *) user_ctx;

    if (lcd->transfer_done_cb != NULL)
    {
        lcd->transfer_done_cb(lcd->transfer_done_user_data);
    }

    return false;
}

static void lcd_SPI_init(void)
{
    spi_bus_config_t bus_conf = {
        .sclk_io_num = BOARD_LCD_SCK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_conf, SPI_DMA_CH_AUTO));
    lcd->bus_initialized = true;
}

static void lcd_SPI_deinit(void)
{
    if (lcd->bus_initialized){
        esp_err_t result = spi_bus_free(SPI2_HOST);
        lcd->bus_initialized = false;
    }
}

void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *data)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcd->panel, x_start, y_start, x_end, y_end, data));
}

static void clear(void)
{
    size_t buf_size = BOARD_LCD_H_RES;
    uint16_t *buf = heap_caps_calloc(1, buf_size * sizeof(uint16_t), MALLOC_CAP_DMA);
    assert(buf);

    for (int line = 0; line < BOARD_LCD_V_RES; line++){ // Blit lines to the screen
        lcd_draw_bitmap(0, line, BOARD_LCD_H_RES, line + 1, buf);
    }

    heap_caps_free(buf);
}

mp_obj_t lcd_init(void)
{
    if(!lcd){
        lcd = calloc(1, sizeof(lcd_t));

        lcd_SPI_init();

        esp_lcd_panel_io_spi_config_t io_config = {
            .dc_gpio_num = BOARD_LCD_DC,
            .cs_gpio_num = BOARD_LCD_CS,
            .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
            .lcd_cmd_bits = BOARD_LCD_CMD_BITS,
            .lcd_param_bits = BOARD_LCD_PARAM_BITS,
            .spi_mode = 0,
            .trans_queue_depth = 10,
            .on_color_trans_done = on_color_trans_done_cb,
            .user_ctx = lcd,
        };
    
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &lcd->io_handle));
    
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = BOARD_LCD_RST,
            .rgb_ele_order = BOARD_LCD_RGB_ELE_ORDER,
            .bits_per_pixel = 16,
        };
    
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(lcd->io_handle, &panel_config, &lcd->panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd->panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(lcd->panel));
    
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcd->panel, BOARD_LCD_INVERT));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(lcd->panel, BOARD_LCD_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd->panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(lcd->panel, BOARD_LCD_GAP_X, BOARD_LCD_GAP_Y));
    
        clear();
    
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd->panel, true));
    }

    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(lcd_init_obj, lcd_init);

static mp_obj_t lcd_deinit(void)
{
    if(lcd){
        if(lcd->panel != NULL){
            ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd->panel, false));
            ESP_ERROR_CHECK(esp_lcd_panel_del(lcd->panel));
            lcd->panel = NULL;
        }
    
        if(lcd->io_handle != NULL){
            ESP_ERROR_CHECK(esp_lcd_panel_io_del(lcd->io_handle));
            lcd->io_handle = NULL;
            lcd_SPI_deinit();
        }

        free(lcd);
    }

    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(lcd_deinit_obj, lcd_deinit);

static mp_obj_t draw_logo(void)
{
    uint16_t *pixels = (uint16_t *)heap_caps_malloc((logo_en_320x172_lcd_width * logo_en_320x172_lcd_height) * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (NULL == pixels)
    {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
        return mp_const_none;
    }
    memcpy(pixels, logo_en_320x172_lcd, (logo_en_320x172_lcd_width * logo_en_320x172_lcd_height) * sizeof(uint16_t));
    esp_lcd_panel_draw_bitmap(lcd->panel, 0, 0, logo_en_320x172_lcd_width, logo_en_320x172_lcd_height, (uint16_t *)pixels);
    heap_caps_free(pixels);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(display_draw_logo_obj, draw_logo);

static const mp_rom_map_elem_t lcd_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_lcd) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&lcd_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&lcd_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&lcd_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&lcd_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_logo), MP_ROM_PTR(&display_draw_logo_obj) },
};

static MP_DEFINE_CONST_DICT(lcd_module_globals, lcd_module_globals_table);

// Define module object.
const mp_obj_module_t lcd_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lcd_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_lcd, lcd_cmodule);
