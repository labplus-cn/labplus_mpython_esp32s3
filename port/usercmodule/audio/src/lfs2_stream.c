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

#include "errno.h"
#include <stddef.h>
#include <string.h>

#include "audio_element.h"
#include "audio_error.h"
#include "audio_mem.h"

#include "esp_log.h"
#include "lfs2_stream.h"
#include "vfs_lfs2.h"
#include "wav_head.h"

#include "py/builtin.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/stream.h"

#define FILE_WAV_SUFFIX_TYPE   "wav"
#define FILE_OPUS_SUFFIX_TYPE  "opus"
#define FILE_AMR_SUFFIX_TYPE   "amr"
#define FILE_AMRWB_SUFFIX_TYPE "Wamr"

static const char *TAG = "LFS2_STREAM";

typedef enum {
    STREAM_TYPE_UNKNOW,
    STREAM_TYPE_WAV,
    STREAM_TYPE_OPUS,
    STREAM_TYPE_AMR,
    STREAM_TYPE_AMRWB,
} wr_stream_type_t;

typedef struct lfs2_stream {
    audio_stream_type_t type;
    int block_size;
    bool is_open;
    wr_stream_type_t w_type;
    mp_obj_vfs_lfs2_file_t *lfs2_file;
} lfs2_stream_t;

static wr_stream_type_t get_type(const char *str)
{
    char *relt = strrchr(str, '.');
    if (relt != NULL) {
        relt++;
        ESP_LOGD(TAG, "result = %s", relt);
        if (strncasecmp(relt, FILE_WAV_SUFFIX_TYPE, 3) == 0) {
            return STREAM_TYPE_WAV;
        } else if (strncasecmp(relt, FILE_OPUS_SUFFIX_TYPE, 4) == 0) {
            return STREAM_TYPE_OPUS;
        } else if (strncasecmp(relt, FILE_AMR_SUFFIX_TYPE, 3) == 0) {
            return STREAM_TYPE_AMR;
        } else if (strncasecmp(relt, FILE_AMRWB_SUFFIX_TYPE, 4) == 0) {
            return STREAM_TYPE_AMRWB;
        } else {
            return STREAM_TYPE_UNKNOW;
        }
    } else {
        return STREAM_TYPE_UNKNOW;
    }
}

static esp_err_t _lfs2_open(audio_element_handle_t self)
{
    lfs2_stream_t *vfs = (lfs2_stream_t *)audio_element_getdata(self);

    audio_element_info_t info;
    char *uri = audio_element_get_uri(self);
    if (uri == NULL) {
        ESP_LOGE(TAG, "Error, uri is not set");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "_lfs2_open, uri:%s", uri);
    char *path = strstr(uri, "/sd");
    audio_element_getinfo(self, &info);
    
    if (path == NULL) {
        ESP_LOGE(TAG, "Error, need file path to open");
        return ESP_FAIL;
    }
    if (vfs->is_open) {
        ESP_LOGE(TAG, "already opened");
        return ESP_FAIL;
    }
    path += strlen("/sd");
    if (vfs->type == AUDIO_STREAM_READER) {
        vfs->lfs2_file = vfs_lfs2_file_open( path, LFS2_O_RDONLY);
        if (vfs->lfs2_file) {
            struct lfs2_info fno = { 0 };
            lfs2_stat(&vfs->lfs2_file->vfs->lfs, path, &fno); 
            info.total_bytes = fno.size;
            ESP_LOGI(TAG, "File size: %d byte, file position: %d", (int)fno.size, (int)info.byte_pos);
        } else {
            ESP_LOGE(TAG, "failed to open %s", path);
            return ESP_FAIL;
        }
    } else if (vfs->type == AUDIO_STREAM_WRITER) {
        vfs->lfs2_file = vfs_lfs2_file_open( path, LFS2_O_WRONLY | LFS2_O_CREAT);
        if (vfs->lfs2_file) {
            vfs->w_type = get_type(path);
            if ((STREAM_TYPE_WAV == vfs->w_type)) {
                wav_header_t info = {0};
                lfs2_file_write(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file, (uint8_t *)&info, sizeof(wav_header_t));
                lfs2_file_sync(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file);
            } else if ((STREAM_TYPE_AMR == vfs->w_type)) {
                lfs2_file_write(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file, "#!AMR\n", 6);
                lfs2_file_sync(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file);
            } else if ((STREAM_TYPE_AMRWB == vfs->w_type)) {
                lfs2_file_write(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file, "#!AMR-WB\n", 9);
                lfs2_file_sync(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file);
            }
        } else {
            ESP_LOGE(TAG, "failed to open %s", path);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "vfs must be Reader or Writer");
        return ESP_FAIL;
    }

    vfs->is_open = true;
    int ret = audio_element_set_total_bytes(self, info.total_bytes);
    return ret;
}

static int _lfs2_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    lfs2_stream_t *vfs = (lfs2_stream_t *)audio_element_getdata(self);
    audio_element_info_t info;
    audio_element_getinfo(self, &info);

    ESP_LOGD(TAG, "read len=%d, pos=%d/%d", len, (int)info.byte_pos, (int)info.total_bytes);

    size_t rlen = lfs2_file_read(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file, buffer, len);
    if (rlen <= 0) {
        ESP_LOGW(TAG, "No more data,ret:%d", rlen);
        rlen = 0;
    } else {
        info.byte_pos += rlen;
        audio_element_setinfo(self, &info);
    }
    return rlen;
}

static int _lfs2_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    lfs2_stream_t *vfs = (lfs2_stream_t *)audio_element_getdata(self);
    audio_element_info_t info;
    audio_element_getinfo(self, &info);
    size_t wlen = 0;
    wlen = lfs2_file_write(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file, (uint8_t *)buffer, len);
    // lfs2_file_sync(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file);
    ESP_LOGD(TAG, "mp_stream_posix_write,%d, errno:%d,pos:%d", wlen, errno, (int)info.byte_pos);
    if (wlen > 0) {
        info.byte_pos += wlen;
        audio_element_setinfo(self, &info);
    }
    return wlen;
}

static int _lfs2_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;
    if (r_size > 0) {
        w_size = audio_element_output(self, in_buffer, r_size);
    } else {
        w_size = r_size;
    }
    return w_size;
}

static esp_err_t _lfs2_close(audio_element_handle_t self)
{
    lfs2_stream_t *vfs = (lfs2_stream_t *)audio_element_getdata(self);

    if (AUDIO_STREAM_WRITER == vfs->type
        && STREAM_TYPE_WAV == vfs->w_type) {
        wav_header_t *wav_info = (wav_header_t *)audio_malloc(sizeof(wav_header_t));

        AUDIO_MEM_CHECK(TAG, wav_info, return ESP_ERR_NO_MEM);

        lfs2_file_seek(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file, 0, LFS2_SEEK_SET);
        audio_element_info_t info;
        size_t bw = 0;
        audio_element_getinfo(self, &info);
        wav_head_init(wav_info, info.sample_rates, info.bits, info.channels);
        wav_head_size(wav_info, (uint32_t)info.byte_pos);
        bw = lfs2_file_write(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file, (uint8_t *)wav_info, sizeof(wav_header_t));
        lfs2_file_sync(&vfs->lfs2_file->vfs->lfs, &vfs->lfs2_file->file);
        // vfs_lfs2_file_close(vfs->lfs2_file);
        audio_free(wav_info);
    }

    if (vfs->is_open) {
        vfs_lfs2_file_close(vfs->lfs2_file);
        vfs->is_open = false;
    }
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_report_info(self);
        audio_element_info_t info = { 0 };
        audio_element_getinfo(self, &info);
        info.byte_pos = 0;
        audio_element_setinfo(self, &info);
    }
    return ESP_OK;
}

static esp_err_t _lfs2_destroy(audio_element_handle_t self)
{
    lfs2_stream_t *vfs = (lfs2_stream_t *)audio_element_getdata(self);
    audio_free(vfs);
    return ESP_OK;
}

audio_element_handle_t lfs2_stream_init(lfs2_stream_cfg_t *config)
{
    audio_element_handle_t el;
    lfs2_stream_t *vfs = audio_calloc(1, sizeof(lfs2_stream_t));
    AUDIO_MEM_CHECK(TAG, vfs, return NULL);

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open = _lfs2_open;
    cfg.close = _lfs2_close;
    cfg.process = _lfs2_process;
    cfg.destroy = _lfs2_destroy;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.out_rb_size = config->out_rb_size;
    cfg.buffer_len = config->buf_sz;
    if (cfg.buffer_len == 0) {
        cfg.buffer_len = LFS2_STREAM_BUF_SIZE;
    }

    cfg.tag = "file";
    vfs->type = config->type;

    if (config->type == AUDIO_STREAM_WRITER) {
        cfg.write = _lfs2_write;
    } else {
        cfg.read = _lfs2_read;
    }
    el = audio_element_init(&cfg);

    AUDIO_MEM_CHECK(TAG, el, goto _lfs2_init_exit);
    audio_element_setdata(el, vfs);
    return el;
_lfs2_init_exit:
    audio_free(vfs);
    return NULL;
}
