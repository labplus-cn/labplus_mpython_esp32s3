

#include "who_human_face_detection.hpp"

extern "C"{
#include <py/objstr.h>

#include "who_camera.h"

#include "who_lcd.h"
#include "test.h"

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueLCDFrame = NULL;

mp_obj_t esp_who_test(void)
{
    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueLCDFrame = xQueueCreate(2, sizeof(camera_fb_t *));

    register_camera(PIXFORMAT_RGB565, FRAMESIZE_240X240, 2, xQueueAIFrame);
    register_human_face_detection(xQueueAIFrame, NULL, NULL, xQueueLCDFrame, false);
    // register_cat_face_detection(xQueueAIFrame, NULL, NULL, xQueueLCDFrame, false);
    // register_lcd(xQueueLCDFrame, NULL, true);
    return mp_const_none; 
}

}

