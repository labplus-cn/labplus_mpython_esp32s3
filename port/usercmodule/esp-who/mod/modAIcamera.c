#include "py/runtime.h"
#include "who_camera.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lcd.h"

camera_fb_t *frame = NULL;

static mp_obj_t reset(void)
{
    camera_init(PIXFORMAT_RGB565, FRAMESIZE_320X172, 2);

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensor_reset_obj, reset);

static mp_obj_t free_fb(void)
{
    esp_camera_fb_return(frame);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensor_free_fb_obj, free_fb);

mp_obj_t sensor_init(void)
{
    if(!frame){
        frame = calloc(1, sizeof(camera_fb_t));
    }
    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensor_init_obj, sensor_init);

mp_obj_t sensor_deinit(void)
{
    if(frame){
        free(frame);
    }
    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensor_deinit_obj, sensor_deinit);

static const mp_rom_map_elem_t AIcamera_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_AIcamera) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&sensor_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&sensor_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_frame_width), MP_ROM_PTR(&get_frame_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&sensor_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(AIcamera_module_globals, AIcamera_module_globals_table);

// Define module object.
const mp_obj_module_t AIcamera_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&AIcamera_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_sensor, AIcamera_cmodule);
