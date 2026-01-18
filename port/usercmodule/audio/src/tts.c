/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

// #include "esp_timer.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "filter_resample.h"

// #include "raw_stream.h"
#include "i2s_stream.h"
#include "tts_stream.h"

// #include "board.h"
#include "modaudio.h"
// #include "mpconfigboard.h"
#include "tts.h"

#define TAG "TTS_MODULE"
#define CHUNK_SIZE 200

void tts_generate(const char *text)
{
    int16_t text_len = strlen(text);

    audio_pipeline_handle_t pipeline;
    audio_element_handle_t tts_stream_reader, i2s_stream_writer;

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    tts_stream_cfg_t tts_cfg = TTS_STREAM_CFG_DEFAULT();
    tts_cfg.type = AUDIO_STREAM_READER;
    tts_stream_reader = tts_stream_init(&tts_cfg);
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(I2S_NUM_0, 16000, I2S_DATA_BIT_WIDTH_16BIT, AUDIO_STREAM_WRITER);
    i2s_cfg.task_core = 1;
    i2s_cfg.uninstall_drv = false;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    // i2s_stream_set_clk(i2s_stream_writer, 16000, 16, 1);
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 16000;
    rsp_cfg.src_ch = 1;
    rsp_cfg.task_core = 1;
    rsp_cfg.dest_ch = 2;
    rsp_cfg.dest_rate = 16000;
    audio_element_handle_t filter = rsp_filter_init(&rsp_cfg);

    audio_pipeline_register(pipeline, tts_stream_reader, "tts");
    audio_pipeline_register(pipeline, filter, "filter");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");
    // Link it together [strings]-->tts_stream-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"tts", "filter", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    char *buffer = heap_caps_calloc(CHUNK_SIZE + 1, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    assert(buffer != NULL);
    size_t copy_len = (CHUNK_SIZE < text_len) ? CHUNK_SIZE : text_len;
    memcpy(buffer, text, copy_len);
    buffer[copy_len] = '\0';  // 确保字符串结尾
    tts_stream_set_strings(tts_stream_reader, buffer);
    text_len -= copy_len;
    size_t offset = copy_len;

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    audio_pipeline_run(pipeline);

    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        // printf("audio msg, type:%d source:%p wr_el:%p cmd:%d data:%d\n", msg.source_type, msg.source, i2s_stream_writer, msg.cmd, (int)msg.data);
        if (ret != ESP_OK) {
            mp_raise_ValueError(MP_ERROR_TEXT("audio enent iface listen error."));
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
                && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
                && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
                if(text_len > 0){
                    audio_pipeline_reset_ringbuffer(pipeline);
                    audio_pipeline_reset_elements(pipeline);
                    copy_len = (CHUNK_SIZE < text_len) ? CHUNK_SIZE : text_len;
                    memcpy(buffer, text + offset, copy_len);
                    buffer[copy_len] = '\0';  // 确保字符串结尾
                    tts_stream_set_strings(tts_stream_reader, buffer);
                    text_len -= copy_len;
                    offset += copy_len;
                    tts_stream_set_strings(tts_stream_reader, buffer);
                    audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
                    audio_pipeline_run(pipeline);
                }else{
                    break;
                }
        }
    }

    heap_caps_free(buffer);
    audio_pipeline_wait_for_stop(pipeline);
    // printf("audio pipeline stopped.\n");
    audio_pipeline_terminate(pipeline);
    audio_pipeline_unregister(pipeline, tts_stream_reader);
    audio_pipeline_unregister(pipeline, filter);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(tts_stream_reader);
}



