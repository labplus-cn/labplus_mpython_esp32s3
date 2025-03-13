#include "py/runtime.h"
#include "who_lcd.h"
#include "esp_camera.h"

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


static mp_obj_t init(void)
{
    display_init();

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(display_init_obj, init);

static mp_obj_t draw_logo(void)
{
    display_draw_logo();

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(display_draw_logo_obj, draw_logo);

static mp_obj_t draw_image(mp_obj_t image)
{
    camera_fb_t *frame = MP_OBJ_TO_PTR(image);
    display_draw_image(frame);

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_draw_image_obj, draw_image);

static mp_obj_t draw_color(mp_obj_t color)
{
    int _color = mp_obj_get_int(color);
    display_set_color(_color);

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_draw_color_obj, draw_color);

static const mp_rom_map_elem_t display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&display_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_logo), MP_ROM_PTR(&display_draw_logo_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_image), MP_ROM_PTR(&display_draw_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_color), MP_ROM_PTR(&display_draw_color_obj) },
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
static MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

// Define module object.
const mp_obj_module_t display_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_display, display_cmodule);
