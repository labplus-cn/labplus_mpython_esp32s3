#include "py/runtime.h"
#include "who_lcd.h"
#include "esp_camera.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "esp_log.h"

static void rgb565_swap(void * buf, uint32_t buf_size_px, void * dst_buf)
{
    uint32_t u32_cnt = buf_size_px / 2;
    uint16_t * buf16 = buf;
    uint16_t * d_buf16 = dst_buf;
    uint32_t * buf32 = buf;
    uint32_t * d_buf32 = dst_buf;

    while(u32_cnt >= 32) {
        d_buf32[0] = ((buf32[0] & 0xff00ff00) >> 8) | ((buf32[0] & 0x00ff00ff) << 8);
        d_buf32[1] = ((buf32[1] & 0xff00ff00) >> 8) | ((buf32[1] & 0x00ff00ff) << 8);
        d_buf32[2] = ((buf32[2] & 0xff00ff00) >> 8) | ((buf32[2] & 0x00ff00ff) << 8);
        d_buf32[3] = ((buf32[3] & 0xff00ff00) >> 8) | ((buf32[3] & 0x00ff00ff) << 8);
        d_buf32[4] = ((buf32[4] & 0xff00ff00) >> 8) | ((buf32[4] & 0x00ff00ff) << 8);
        d_buf32[5] = ((buf32[5] & 0xff00ff00) >> 8) | ((buf32[5] & 0x00ff00ff) << 8);
        d_buf32[6] = ((buf32[6] & 0xff00ff00) >> 8) | ((buf32[6] & 0x00ff00ff) << 8);
        d_buf32[7] = ((buf32[7] & 0xff00ff00) >> 8) | ((buf32[7] & 0x00ff00ff) << 8);
        d_buf32[8] = ((buf32[8] & 0xff00ff00) >> 8) | ((buf32[8] & 0x00ff00ff) << 8);
        d_buf32[9] = ((buf32[9] & 0xff00ff00) >> 8) | ((buf32[9] & 0x00ff00ff) << 8);
        d_buf32[10] = ((buf32[10] & 0xff00ff00) >> 8) | ((buf32[10] & 0x00ff00ff) << 8);
        d_buf32[11] = ((buf32[11] & 0xff00ff00) >> 8) | ((buf32[11] & 0x00ff00ff) << 8);
        d_buf32[12] = ((buf32[12] & 0xff00ff00) >> 8) | ((buf32[12] & 0x00ff00ff) << 8);
        d_buf32[13] = ((buf32[13] & 0xff00ff00) >> 8) | ((buf32[13] & 0x00ff00ff) << 8);
        d_buf32[14] = ((buf32[14] & 0xff00ff00) >> 8) | ((buf32[14] & 0x00ff00ff) << 8);
        d_buf32[15] = ((buf32[15] & 0xff00ff00) >> 8) | ((buf32[15] & 0x00ff00ff) << 8);
        d_buf32[16] = ((buf32[16] & 0xff00ff00) >> 8) | ((buf32[16] & 0x00ff00ff) << 8);
        d_buf32[17] = ((buf32[17] & 0xff00ff00) >> 8) | ((buf32[17] & 0x00ff00ff) << 8);
        d_buf32[18] = ((buf32[18] & 0xff00ff00) >> 8) | ((buf32[18] & 0x00ff00ff) << 8);
        d_buf32[19] = ((buf32[19] & 0xff00ff00) >> 8) | ((buf32[19] & 0x00ff00ff) << 8);
        d_buf32[20] = ((buf32[20] & 0xff00ff00) >> 8) | ((buf32[20] & 0x00ff00ff) << 8);
        d_buf32[21] = ((buf32[21] & 0xff00ff00) >> 8) | ((buf32[21] & 0x00ff00ff) << 8);
        d_buf32[22] = ((buf32[22] & 0xff00ff00) >> 8) | ((buf32[22] & 0x00ff00ff) << 8);
        d_buf32[23] = ((buf32[23] & 0xff00ff00) >> 8) | ((buf32[23] & 0x00ff00ff) << 8);
        d_buf32[24] = ((buf32[24] & 0xff00ff00) >> 8) | ((buf32[24] & 0x00ff00ff) << 8);
        d_buf32[25] = ((buf32[25] & 0xff00ff00) >> 8) | ((buf32[25] & 0x00ff00ff) << 8);
        d_buf32[26] = ((buf32[26] & 0xff00ff00) >> 8) | ((buf32[26] & 0x00ff00ff) << 8);
        d_buf32[27] = ((buf32[27] & 0xff00ff00) >> 8) | ((buf32[27] & 0x00ff00ff) << 8);
        d_buf32[28] = ((buf32[28] & 0xff00ff00) >> 8) | ((buf32[28] & 0x00ff00ff) << 8);
        d_buf32[29] = ((buf32[29] & 0xff00ff00) >> 8) | ((buf32[29] & 0x00ff00ff) << 8);
        d_buf32[30] = ((buf32[30] & 0xff00ff00) >> 8) | ((buf32[30] & 0x00ff00ff) << 8);
        d_buf32[31] = ((buf32[31] & 0xff00ff00) >> 8) | ((buf32[31] & 0x00ff00ff) << 8);
        buf32 += 32;
        d_buf32 += 32;
        u32_cnt -= 32;
    }

    while(u32_cnt) {
        *d_buf32 = ((*buf32 & 0xff00ff00) >> 8) | ((*buf32 & 0x00ff00ff) << 8);
        buf32++;
        d_buf32++;
        u32_cnt--;
    }

    if(buf_size_px & 0x1) {
        uint32_t e = buf_size_px - 1;
        d_buf16[e] = ((buf16[e] & 0xff00) >> 8) | ((buf16[e] & 0x00ff) << 8);
    }

}

static mp_obj_t mp_lcd_init(void)
{
    lcd_init();
    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_lcd_init_obj, mp_lcd_init);

static mp_obj_t mp_lcd_deinit(void)
{
    lcd_deinit();
    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_lcd_deinit_obj, mp_lcd_deinit);

static mp_obj_t mp_lcd_draw_logo(void)
{
    lcd_draw_logo();
    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_lcd_draw_logo_obj, mp_lcd_draw_logo);

// static mp_obj_t draw_image(mp_obj_t image)
// {
//     camera_fb_t *frame = MP_OBJ_TO_PTR(image);
//     display_draw_image(frame);

//     return mp_const_none; 
// }
// static MP_DEFINE_CONST_FUN_OBJ_1(display_draw_image_obj, draw_image);

static mp_obj_t mp_lcd_show(mp_obj_t buf_obj)
{
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_obj, &bufinfo, MP_BUFFER_READ);
    
    lcd_t *lcd = get_lcd_handle();
    // lv_draw_sw_rgb565_swap(bufinfo.buf, bufinfo.len / 2);
    uint16_t *buf_tmp = (uint16_t *)m_malloc(bufinfo.len);
    rgb565_swap(bufinfo.buf, bufinfo.len / 2, (void *)buf_tmp);
    esp_lcd_panel_draw_bitmap(lcd->panel, 0, 0, 320, 172, buf_tmp); //(uint16_t *)bufinfo.buf);

    m_free(buf_tmp);
    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_show_obj, mp_lcd_show);

static mp_obj_t mp_lcd_draw_color(mp_obj_t color)
{
    int _color = mp_obj_get_int(color);
    lcd_set_color(_color);

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_draw_color_obj, mp_lcd_draw_color);

static const mp_rom_map_elem_t lcd_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_lcd) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mp_lcd_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&mp_lcd_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_lcd_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&mp_lcd_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_logo), MP_ROM_PTR(&mp_lcd_draw_logo_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_color), MP_ROM_PTR(&mp_lcd_draw_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&mp_lcd_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_PINK), MP_ROM_INT(GUI_Pink) },
    { MP_ROM_QSTR(MP_QSTR_DEEP_PINK), MP_ROM_INT(GUI_DeepPink) },
    { MP_ROM_QSTR(MP_QSTR_PURPLE), MP_ROM_INT(GUI_Purple) },
    { MP_ROM_QSTR(MP_QSTR_BLUE), MP_ROM_INT(GUI_Blue) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_BLUE), MP_ROM_INT(GUI_LightBLue) },
    { MP_ROM_QSTR(MP_QSTR_MEDIUM_BLUE), MP_ROM_INT(GUI_MediumBlue) },
    { MP_ROM_QSTR(MP_QSTR_DARK_BLUE), MP_ROM_INT(GUI_DarkBlue) },
    { MP_ROM_QSTR(MP_QSTR_SKY_BLUE), MP_ROM_INT(GUI_SkyBlue) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_CYAN), MP_ROM_INT(GUI_LightCyan) },
    { MP_ROM_QSTR(MP_QSTR_DARK_CYAN), MP_ROM_INT(GUI_DarkCyan) },
    { MP_ROM_QSTR(MP_QSTR_GREEN), MP_ROM_INT(GUI_Green) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_GREEN), MP_ROM_INT(GUI_LightGreen) },
    { MP_ROM_QSTR(MP_QSTR_DARK_GREEN), MP_ROM_INT(GUI_DarkGreen) },
    { MP_ROM_QSTR(MP_QSTR_GREEN_YELLOW), MP_ROM_INT(GUI_GreenYellow) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_YELLOW), MP_ROM_INT(GUI_LightYellow) },
    { MP_ROM_QSTR(MP_QSTR_YELLO), MP_ROM_INT(GUI_Yellow) },
    { MP_ROM_QSTR(MP_QSTR_ORANGE), MP_ROM_INT(GUI_Orange) },
    { MP_ROM_QSTR(MP_QSTR_RED), MP_ROM_INT(GUI_Red) },
    { MP_ROM_QSTR(MP_QSTR_DARK_RED), MP_ROM_INT(GUI_DarkRed) },
    { MP_ROM_QSTR(MP_QSTR_WHITE), MP_ROM_INT(GUI_White) },
    { MP_ROM_QSTR(MP_QSTR_GRAY), MP_ROM_INT(GUI_Gray) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_GRAY), MP_ROM_INT(GUI_LightGray) },
    { MP_ROM_QSTR(MP_QSTR_DARK_GRAY), MP_ROM_INT(GUI_DarkGray) },
    { MP_ROM_QSTR(MP_QSTR_BLACK), MP_ROM_INT(GUI_Black) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_SKY_BLUE), MP_ROM_INT(GUI_LightSkyBlue) },
    { MP_ROM_QSTR(MP_QSTR_DEEP_SKY_BLUE), MP_ROM_INT(GUI_DeepSkyBlue) },
    { MP_ROM_QSTR(MP_QSTR_CYAN), MP_ROM_INT(GUI_Cyan) },
    { MP_ROM_QSTR(MP_QSTR_SPRING_GREEN), MP_ROM_INT(GUI_SpringGreen) },
    { MP_ROM_QSTR(MP_QSTR_GOLD), MP_ROM_INT(GUI_Gold) },
    { MP_ROM_QSTR(MP_QSTR_DARK_ORANGE), MP_ROM_INT(GUI_DarkOrange) },
    { MP_ROM_QSTR(MP_QSTR_BROWN), MP_ROM_INT(GUI_Brown) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_GRAY), MP_ROM_INT(GUI_LightGray) },
};

static MP_DEFINE_CONST_DICT(lcd_module_globals, lcd_module_globals_table);

// Define module object.
const mp_obj_module_t lcd_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lcd_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_lcd, lcd_cmodule);
