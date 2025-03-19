#ifndef __LVGL_ESP32_LCD_H__
#define __LVGL_ESP32_LCD_H__

#include "esp_lcd_types.h"
#include "py/obj.h"

#if CONFIG_MPYTHON_PRO_BOARD
    #define BOARD_LCD_MOSI 37
    #define BOARD_LCD_MISO -1
    #define BOARD_LCD_SCK 36
    #define BOARD_LCD_CS 34
    #define BOARD_LCD_DC 35
    #define BOARD_LCD_RST -1
    #define BOARD_LCD_BL 33
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
#elif CONFIG_LABPLUS_LEDONG_V2_BOARD
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
#endif

//---------------------颜色表--------------------
#define GUI_HotPink		            0x56FB		//热情的粉红
#define GUI_DeepPink		        0xB2F8		//深粉色
#define GUI_Purple		            0x1080		//紫色
#define GUI_Blue		            0x1F00		//纯蓝
#define GUI_MediumBlue		        0x1900		//适中的蓝色
#define GUI_DarkBlue		        0x1100		//深蓝色
#define GUI_LightSkyBlue		    0x7F86		//淡蓝色
#define GUI_SkyBlue		            0x7D86		//天蓝色
#define GUI_DeepSkyBlue		        0xFF05		//深天蓝
#define GUI_LightBLue		        0xDCAE		//淡蓝
#define GUI_LightCyan		        0xFFE7		//淡青色
#define GUI_Cyan		            0xFF07		//青色
#define GUI_DarkCyan		        0x5104		//深青色
#define GUI_SpringGreen		        0x8E3D		//春天的绿色
#define GUI_LightGreen		        0x7297		//淡绿色
#define GUI_Green		            0x0004		//纯绿
#define GUI_DarkGreen		        0x2003		//深绿色
#define GUI_GreenYellow		        0xE5AF		//绿黄色
#define GUI_LightYellow		        0xFCFF		//浅黄色
#define GUI_Yellow		            0xE0FF		//纯黄
#define GUI_Gold		            0xA0FE		//金
#define GUI_Orange		            0x20FD		//橙色
#define GUI_DarkOrange		        0x60FC		//深橙色
#define GUI_Red			            0x00F8		//纯红
#define GUI_DarkRed		            0x0088		//深红色
#define GUI_Pink		            0x19FE		//粉红
#define GUI_Brown		            0x45A1		//棕色
#define GUI_White		            0xFFFF		//纯白
#define GUI_LightGray		        0x9AD6		//浅灰色
#define GUI_DarkGray	            0x55AD		//深灰色
#define GUI_Gray		            0x1084		//灰色
#define GUI_Black		            0x0000		//纯黑

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
void display_draw_image(int x, int y, int width, int height, const void *data);

extern lcd_t *lcd;
#endif /* __LVGL_ESP32_LCD_H__ */
