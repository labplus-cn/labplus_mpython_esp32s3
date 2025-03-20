// #include "human_face_detection.hpp"

#include "py/runtime.h"
#include "who_camera.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "who_lcd.h"
extern bool human_face_detection(camera_fb_t *frame);
static void detect_task_handler(void *arg)
{
    camera_fb_t *frame = NULL;

    while(1){
        frame = esp_camera_fb_get();
        if(frame){
            human_face_detection(frame);

            lcd_draw_image(0, 0, frame->width, frame->height, (void *)frame->buf);
            esp_camera_fb_return(frame);
        }
    }
}

static mp_obj_t AI_detect_init(void)
{
    xTaskCreatePinnedToCore(detect_task_handler, "task", 4 * 1024, NULL, 5, NULL, 0);

    return mp_const_none; 
}
static MP_DEFINE_CONST_FUN_OBJ_0(AI_detect_init_obj, AI_detect_init);

static const mp_rom_map_elem_t AIcamera_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_AIcamera) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&AI_detect_init_obj) },
    // { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&sensor_deinit_obj) },
    // { MP_ROM_QSTR(MP_QSTR_face_detact), MP_ROM_PTR(&face_detect_obj) },
};
static MP_DEFINE_CONST_DICT(AIcamera_module_globals, AIcamera_module_globals_table);

// Define module object.
const mp_obj_module_t AIcamera_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&AIcamera_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_AIcamera, AIcamera_cmodule);


