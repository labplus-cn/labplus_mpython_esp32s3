#include "py/runtime.h"
#include "who_lcd.h"
#include "esp_camera.h"

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
static MP_DEFINE_CONST_FUN_OBJ_0(display_draw_image_obj, draw_image);

static const mp_rom_map_elem_t display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&display_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_logo), MP_ROM_PTR(&display_draw_logo_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_image), MP_ROM_PTR(&display_draw_image_obj) },
};
static MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

// Define module object.
const mp_obj_module_t display_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_display, display_cmodule);
