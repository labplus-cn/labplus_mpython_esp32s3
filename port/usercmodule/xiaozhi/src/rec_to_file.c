/*
 * rec_to_file.c - 录音保存为标准 OGG/Opus 文件（RFC 7845）
 *
 * 使用 record_pipe（audio_capture.h）采集 Opus 帧，封装为 OGG 容器写入
 * LittleFS 文件，生成的 .opus 文件可被 VLC、ffplay、mpv 等播放器直接打开。
 *
 * OGG 页结构：页头（27字节）+ 分段表 + Opus 数据；每个页含一个 Opus 包。
 * CRC32：OGG 专用（多项式 0x04c11db7，大端序）。
 *
 * 注意：此函数在 MicroPython 线程中同步执行，期间会阻塞 Python 解释器。
 */

#include "rec_to_file.h"
#include "audio_capture.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "vfs_lfs2.h"          /* vfs_lfs2_file_open/close, mp_obj_vfs_lfs2_file_t */
#include "lib/littlefs/lfs2.h" /* lfs2_file_write */

#include "esp_gmf_app_setup_peripheral.h" /* esp_gmf_app_get_record_handle */
#include "esp_codec_dev.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "rec_to_file"

/* ================================================================
 * OGG CRC32（多项式 0x04c11db7，大端序，RFC 3533）
 * ================================================================ */

static uint32_t s_ogg_crc_tbl[256];
static bool     s_ogg_crc_ready = false;

static void ogg_crc_init(void)
{
    if (s_ogg_crc_ready) return;
    for (int i = 0; i < 256; i++) {
        uint32_t r = (uint32_t)i << 24;
        for (int j = 0; j < 8; j++)
            r = (r & 0x80000000u) ? ((r << 1) ^ 0x04c11db7u) : (r << 1);
        s_ogg_crc_tbl[i] = r;
    }
    s_ogg_crc_ready = true;
}

static uint32_t ogg_crc32(uint32_t crc, const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++)
        crc = (crc << 8) ^ s_ogg_crc_tbl[((crc >> 24) ^ buf[i]) & 0xff];
    return crc;
}

/* ================================================================
 * OGG 页写入（RFC 3533 §6）
 * ================================================================ */

typedef struct {
    uint32_t serial;  /* 逻辑比特流序列号（任意固定值） */
    uint32_t seqno;   /* 页序号（每页递增） */
} ogg_state_t;

/**
 * 写一个 OGG 页（含 CRC）。
 *
 * hdr_type: 0x02=BOS（流首页），0x04=EOS（流末页），0x00=普通页
 * granule:  粒度位置（Opus 用 48kHz 采样数；头部页为 0）
 * data/dlen: Opus 包数据（dlen=0 时写空 EOS 页）
 */
static lfs2_ssize_t ogg_write_page(ogg_state_t *st,
                                    lfs2_t *lfs, lfs2_file_t *file,
                                    uint8_t hdr_type, int64_t granule,
                                    const uint8_t *data, int dlen)
{
    /* 构建分段表（OGG lacing）：每个分段最多 255 字节 */
    uint8_t seg_tbl[256];
    int n_segs = 0;
    if (dlen > 0) {
        int rem = dlen;
        while (rem >= 255 && n_segs < 255) {
            seg_tbl[n_segs++] = 255;
            rem -= 255;
        }
        seg_tbl[n_segs++] = (uint8_t)rem;
    }
    /* dlen==0：n_segs=0，写空页（用于 EOS 终止页） */

    /* 构建 OGG 页头（27 字节固定 + 分段表） */
    int hlen = 27 + n_segs;
    uint8_t hdr[27 + 256];
    memset(hdr, 0, hlen);

    memcpy(hdr + 0, "OggS", 4);            /* capture_pattern */
    hdr[4] = 0x00;                          /* stream_structure_version */
    hdr[5] = hdr_type;                      /* header_type_flag */
    for (int i = 0; i < 8; i++)            /* granule_position (LE 64-bit) */
        hdr[6 + i] = (uint8_t)(granule >> (i * 8));
    for (int i = 0; i < 4; i++)            /* bitstream_serial_number (LE) */
        hdr[14 + i] = (uint8_t)(st->serial >> (i * 8));
    uint32_t sq = st->seqno++;
    for (int i = 0; i < 4; i++)            /* page_sequence_number (LE) */
        hdr[18 + i] = (uint8_t)(sq >> (i * 8));
    /* checksum（字节 22-25）暂置 0，CRC 计算后回填 */
    hdr[26] = (uint8_t)n_segs;             /* number_page_segments */
    memcpy(hdr + 27, seg_tbl, n_segs);     /* segment_table */

    /* CRC：覆盖整个页（头 + 数据），checksum 字段置 0 参与计算 */
    uint32_t crc = 0;
    crc = ogg_crc32(crc, hdr, hlen);
    if (dlen > 0) crc = ogg_crc32(crc, data, dlen);
    hdr[22] = (uint8_t)(crc);
    hdr[23] = (uint8_t)(crc >> 8);
    hdr[24] = (uint8_t)(crc >> 16);
    hdr[25] = (uint8_t)(crc >> 24);

    lfs2_ssize_t w = lfs2_file_write(lfs, file, hdr, hlen);
    if (w < 0) return w;
    if (dlen > 0) {
        lfs2_ssize_t wd = lfs2_file_write(lfs, file, data, dlen);
        if (wd < 0) return wd;
        w += wd;
    }
    return w;
}

/* ================================================================
 * RFC 7845 Opus 头部页
 * ================================================================ */

/* Opus 识别头（§5.1），写在 BOS 页 */
static lfs2_ssize_t ogg_write_opus_id_header(ogg_state_t *st,
                                               lfs2_t *lfs, lfs2_file_t *file,
                                               uint8_t channels,
                                               uint32_t orig_sample_rate,
                                               uint16_t pre_skip)
{
    uint8_t id[19];
    memcpy(id, "OpusHead", 8);
    id[8]  = 0x01;                                /* version */
    id[9]  = channels;
    id[10] = (uint8_t)(pre_skip & 0xff);
    id[11] = (uint8_t)(pre_skip >> 8);
    id[12] = (uint8_t)(orig_sample_rate);         /* 仅供参考，不影响解码 */
    id[13] = (uint8_t)(orig_sample_rate >> 8);
    id[14] = (uint8_t)(orig_sample_rate >> 16);
    id[15] = (uint8_t)(orig_sample_rate >> 24);
    id[16] = 0x00; id[17] = 0x00;                /* output_gain = 0 */
    id[18] = 0x00;                                /* channel_mapping_family = 0（单/双声道） */
    return ogg_write_page(st, lfs, file, 0x02 /* BOS */, 0, id, sizeof(id));
}

/* Opus 注释头（§5.2），最小化：无厂商字符串、无注释 */
static lfs2_ssize_t ogg_write_opus_comment_header(ogg_state_t *st,
                                                    lfs2_t *lfs, lfs2_file_t *file)
{
    uint8_t tags[16];
    memcpy(tags, "OpusTags", 8);
    memset(tags + 8, 0, 8); /* vendor_length=0（4B） + user_comment_list_length=0（4B） */
    return ogg_write_page(st, lfs, file, 0x00, 0, tags, sizeof(tags));
}

/* ================================================================
 * 主录音函数
 * ================================================================ */

/* Opus 帧时长（毫秒）；须与 audio_capture.c 中的配置保持一致 */
#define OPUS_FRAME_MS    60
/* 每帧对应的粒度增量（OGG Opus 粒度固定以 48kHz 为单位） */
#define GRANULE_PER_FRAME  (OPUS_FRAME_MS * 48000 / 1000)  /* 2880 */
/* 原始采样率（仅写入 ID 头的 informational 字段） */
#define OPUS_ORIG_RATE   16000

static volatile int s_wakeup_count = 0;

static void rec_wakeup_cb(void *ctx)
{
    s_wakeup_count++;
    ESP_LOGI(TAG, "*** Wakeup detected! count=%d ***", s_wakeup_count);
}

esp_err_t rec_to_file(const char *path, uint32_t duration_ms,
                      bool use_afe, int *out_wakeup_count)
{
    if (!path || !duration_ms) return ESP_ERR_INVALID_ARG;

    /* 音频硬件句柄（由 audio.init() 初始化） */
    esp_codec_dev_handle_t rec_dev =
        (esp_codec_dev_handle_t)esp_gmf_app_get_record_handle();
    if (!rec_dev) {
        ESP_LOGE(TAG, "Audio hardware not ready. Call audio.init() first.");
        return ESP_ERR_INVALID_STATE;
    }

    /* 打开 LittleFS 文件 */
    mp_obj_vfs_lfs2_file_t *f = vfs_lfs2_file_open(
        path, LFS2_O_WRONLY | LFS2_O_CREAT | LFS2_O_TRUNC);
    if (f == (mp_obj_vfs_lfs2_file_t *)mp_const_none) {
        ESP_LOGE(TAG, "VFS not mounted: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* 初始化 OGG 状态 */
    ogg_crc_init();
    ogg_state_t ogg = { .serial = 0x12345678, .seqno = 0 };
    int64_t granule = 0;  /* 累计粒度（48kHz 采样数） */

    /* 写 Opus 识别头（BOS 页）和注释头 */
    /* pre_skip=312：Opus SILK 模式（16kHz 输入）编码器延迟 = 6.5ms × 48000Hz = 312 采样 */
    ogg_write_opus_id_header(&ogg, &f->vfs->lfs, &f->file, 1, OPUS_ORIG_RATE, 312);
    ogg_write_opus_comment_header(&ogg, &f->vfs->lfs, &f->file);

    /* 打开录音 pipeline */
    record_pipe_handle_t rp = NULL;
    s_wakeup_count = 0;
    record_pipe_wakeup_cb_t wakeup_cb = use_afe ? rec_wakeup_cb : NULL;
    esp_err_t err = record_pipe_open(rec_dev, wakeup_cb, NULL, &rp);
    ESP_LOGI(TAG, "Pipeline: %s", use_afe ? "AFE wakeup detection enabled" : "fallback (no wakeup detection)");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "record_pipe_open failed: %d", err);
        vfs_lfs2_file_close(f);
        return err;
    }

    err = record_pipe_start(rp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "record_pipe_start failed: %d", err);
        record_pipe_close(rp);
        vfs_lfs2_file_close(f);
        return err;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
    uint32_t frames = 0;
    bool write_err = false;

    while (xTaskGetTickCount() < deadline) {
        const uint8_t *data = NULL;
        int size = 0;
        record_pipe_acquire_frame(rp, &data, &size);

        if (size > 0) {
            granule += GRANULE_PER_FRAME;
            lfs2_ssize_t w = ogg_write_page(&ogg, &f->vfs->lfs, &f->file,
                                             0x00, granule, data, size);
            record_pipe_release_frame(rp);
            if (w < 0) {
                ESP_LOGE(TAG, "Write error (disk full?)");
                write_err = true;
                break;
            }
            frames++;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    record_pipe_stop(rp);
    record_pipe_close(rp);

    /* 写 EOS 终止页（空内容，通知播放器流结束） */
    if (!write_err) {
        ogg_write_page(&ogg, &f->vfs->lfs, &f->file, 0x04 /* EOS */, granule, NULL, 0);
    }

    vfs_lfs2_file_close(f);

    if (out_wakeup_count) {
        *out_wakeup_count = use_afe ? s_wakeup_count : 0;
    }

    if (write_err) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Saved %lu Opus frames → %s%s",
             (unsigned long)frames, path,
             use_afe ? "" : " (wakeup detection disabled)");
    if (use_afe) {
        ESP_LOGI(TAG, "Wakeup count: %d", s_wakeup_count);
    }
    return ESP_OK;
}
