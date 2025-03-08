

#include "who_human_face_detection.hpp"
extern "C"{
#include <examplemodule.h>
#include <py/objstr.h>

#include "who_camera.h"

#include "who_lcd.h"
#include "examplemodule.h"

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueLCDFrame = NULL;

extern "C" void test()
{
    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueLCDFrame = xQueueCreate(2, sizeof(camera_fb_t *));

    register_camera(PIXFORMAT_RGB565, FRAMESIZE_240X240, 2, xQueueAIFrame);
    register_human_face_detection(xQueueAIFrame, NULL, NULL, xQueueLCDFrame, false);
    // register_cat_face_detection(xQueueAIFrame, NULL, NULL, xQueueLCDFrame, false);
    // register_lcd(xQueueLCDFrame, NULL, true);
}

mp_obj_t esp_who_init(void)
{
    // recorder_deinit();
    test();
    return mp_const_none; 
}
}





// static mp_obj_t recorder_deinit(void)
// {
//     // recorder_deinit();
    
//     return mp_const_none; 
// }
// static MP_DEFINE_CONST_FUN_OBJ_0(who_deinit_obj, recorder_deinit);

// static const mp_map_elem_t mpython_test_locals_dict_table[] = {
//     {MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_esp_who)},
//     {MP_OBJ_NEW_QSTR(MP_QSTR___init__), (mp_obj_t)&who_init_obj},
//     // {MP_OBJ_NEW_QSTR(MP_QSTR_deinit), (mp_obj_t)&who_deinit_obj},
// };

// static MP_DEFINE_CONST_DICT(mpython_test_locals_dict, mpython_test_locals_dict_table);

// const mp_obj_module_t mp_module_who_test = {
//     .base = {&mp_type_module},
//     .globals = (mp_obj_dict_t *)&mpython_test_locals_dict,
// };

// MP_REGISTER_MODULE(MP_QSTR_esp_who_test, mp_module_who_test);
