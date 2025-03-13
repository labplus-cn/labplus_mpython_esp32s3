#include "py/runtime.h"
#include "who_lcd.h"
#include "esp_camera.h"

//---------------------颜色表--------------------
#define GUI_Pink		            0xce1f		//粉红
#define GUI_LightPink		        0xc5bf		//浅粉红
#define GUI_DeepPink		        0x90bf		//深粉色
#define GUI_Purple		            0x8010		//紫色
#define GUI_MediumPurple		    0xdb92		//适中的紫色
#define GUI_Blue		            0xf800		//纯蓝
#define GUI_LightBLue		        0xe6d5		//淡蓝
#define GUI_MediumBlue		        0xc800		//适中的蓝色
#define GUI_DarkBlue		        0x8800		//深蓝色
#define GUI_SkyBlue		            0xee70		//天蓝色
#define GUI_Cyan		            0xffe0		//青色
#define GUI_LightCyan		        0xfffc		//淡青色
#define GUI_DarkCyan		        0x8c40		//深青色
#define GUI_Green		            0x0400		//纯绿
#define GUI_LightGreen		        0x9772		//淡绿色
#define GUI_DarkGreen		        0x0320		//深绿色
#define GUI_GreenYellow		        0x2ff5		//绿黄色
#define GUI_LightYellow		        0xe7ff		//浅黄色
#define GUI_Yellow		            0x07ff		//纯黄
#define GUI_Gold		            0x06bf		//金
#define GUI_Orange		            0x053f		//橙色
#define GUI_Red			            0x001f		//纯红
#define GUI_DarkRed		            0x0011		//深红色
#define GUI_Brown		            0x2954		//棕色
#define GUI_White		            0xffff		//纯白
#define GUI_Gray		            0x8410		//灰色
#define GUI_LightGray		    0xd69a		//浅灰色
#define GUI_DarkGray	            0xad55		//深灰色
#define GUI_Black		            0x0000      //纯黑

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
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&display_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_logo), MP_ROM_PTR(&display_draw_logo_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_image), MP_ROM_PTR(&display_draw_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_color), MP_ROM_PTR(&display_draw_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_PINK), MP_ROM_INT(GUI_Pink) },
    { MP_ROM_QSTR(MP_QSTR_LIGHT_PINK), MP_ROM_INT(GUI_LightPink) },
    { MP_ROM_QSTR(MP_QSTR_DEEP_PINK), MP_ROM_INT(GUI_DeepPink) },
    { MP_ROM_QSTR(MP_QSTR_PURPLE), MP_ROM_INT(GUI_Purple) },
    { MP_ROM_QSTR(MP_QSTR_MEDIUM_PURPLE), MP_ROM_INT(GUI_MediumPurple) },
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
};
static MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

// Define module object.
const mp_obj_module_t display_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_display, display_cmodule);
