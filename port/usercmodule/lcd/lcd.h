#ifndef __LVGL_ESP32_LCD_H__
#define __LVGL_ESP32_LCD_H__

#include "esp_lcd_types.h"
#include "py/obj.h"

#define BOARD_LCD_MOSI 37
#define BOARD_LCD_MISO -1
#define BOARD_LCD_SCK 36
#define BOARD_LCD_CS -1
#define BOARD_LCD_DC 35
#define BOARD_LCD_RST -1
#define BOARD_LCD_BL -1
#define BOARD_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define BOARD_LCD_BK_LIGHT_ON_LEVEL 0
#define BOARD_LCD_BK_LIGHT_OFF_LEVEL !BOARD_LCD_BK_LIGHT_ON_LEVEL
#define BOARD_LCD_H_RES 320
#define BOARD_LCD_V_RES 172
#define BOARD_LCD_CMD_BITS 8
#define BOARD_LCD_PARAM_BITS 8
#define BOARD_LCD_RGB_ELE_ORDER LCD_RGB_ENDIAN_BGR
#define BOARD_LCD_SWAP_XY    true
#define BOARD_LCD_MIRROR_X   true
#define BOARD_LCD_MIRROR_Y   true
#define BOARD_LCD_GAP_X      0
#define BOARD_LCD_GAP_Y      34
#define BOARD_LCD_INVERT     false

typedef void (*lvgl_esp32_transfer_done_cb_t)(void *);

typedef struct lcd_t
{
    bool bus_initialized;

    lvgl_esp32_transfer_done_cb_t transfer_done_cb;
    void *transfer_done_user_data;

    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io_handle;
} lcd_t;

void lvgl_esp32_Display_draw_bitmap(
    lcd_t *self,
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *data
);

mp_obj_t lcd_init(void);

extern lcd_t *lcd;
#endif /* __LVGL_ESP32_LCD_H__ */
