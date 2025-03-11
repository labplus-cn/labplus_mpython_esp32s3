#include "py/runtime.h"
#include "who_camera.h"
#include "esp_camera.h"

camera_fb_t *frame = NULL;

static mp_obj_t reset(void)
{
    camera_init(PIXFORMAT_RGB565, FRAMESIZE_240X240, 2);

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensor_reset_obj, reset);

static mp_obj_t snapshot(void)
{
    frame = esp_camera_fb_get();

    return MP_OBJ_FROM_PTR(frame);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensor_snapshot_obj, snapshot);

static mp_obj_t free_fb(void)
{
    esp_camera_fb_return(frame);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensor_free_fb_obj, free_fb);

static const mp_rom_map_elem_t sensor_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sensor) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&sensor_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_snapshot), MP_ROM_PTR(&sensor_snapshot_obj) },
    { MP_ROM_QSTR(MP_QSTR_free_fb), MP_ROM_PTR(&sensor_free_fb_obj) },
};
static MP_DEFINE_CONST_DICT(sensor_module_globals, sensor_module_globals_table);

// Define module object.
const mp_obj_module_t sensor_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sensor_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_sensor, sensor_cmodule);
