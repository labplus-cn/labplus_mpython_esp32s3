#include "who_human_face_detection.hpp"

#include "esp_log.h"

#include "dl_image.hpp"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "who_ai_utils.hpp"

#define TWO_STAGE_ON 1

static const char *TAG = "human_face_detection";

bool human_face_detection(camera_fb_t *frame)
{
    HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);
#if TWO_STAGE_ON
    HumanFaceDetectMNP01 detector2(0.4F, 0.3F, 10);
#endif

    bool is_detected = false;

#if TWO_STAGE_ON
        std::list<dl::detect::result_t> &detect_candidates = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
        std::list<dl::detect::result_t> &detect_results = detector2.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_candidates);
#else
        std::list<dl::detect::result_t> &detect_results = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
#endif

        if (detect_results.size() > 0)
        {
            draw_detection_result((uint16_t *)frame->buf, frame->height, frame->width, detect_results);
            print_detection_result(detect_results);
            is_detected = true;
        }

    return is_detected;
}

