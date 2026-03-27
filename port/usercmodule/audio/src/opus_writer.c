/*
 * opus_writer.c - OPUS format writer (OGG/Opus container)
 */

#include "format_writer.h"
#include <string.h>
#include <stdbool.h>

/* OGG CRC32 table */
static uint32_t s_ogg_crc_tbl[256];
static bool s_ogg_crc_ready = false;

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

typedef struct {
    uint32_t serial;
    uint32_t seqno;
} ogg_state_t;

/* Write OGG page */
static void ogg_write_page(ogg_state_t *st, FILE *file,
                           uint8_t hdr_type, int64_t granule,
                           const uint8_t *data, int dlen)
{
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

    int hlen = 27 + n_segs;
    uint8_t hdr[27 + 256];
    memset(hdr, 0, hlen);

    memcpy(hdr, "OggS", 4);
    hdr[4] = 0x00;
    hdr[5] = hdr_type;
    for (int i = 0; i < 8; i++)
        hdr[6 + i] = (uint8_t)(granule >> (i * 8));
    for (int i = 0; i < 4; i++)
        hdr[14 + i] = (uint8_t)(st->serial >> (i * 8));
    for (int i = 0; i < 4; i++)
        hdr[18 + i] = (uint8_t)(st->seqno >> (i * 8));
    hdr[26] = (uint8_t)n_segs;
    memcpy(hdr + 27, seg_tbl, n_segs);

    uint32_t crc = ogg_crc32(0, hdr, hlen);
    if (dlen > 0) crc = ogg_crc32(crc, data, dlen);
    for (int i = 0; i < 4; i++)
        hdr[22 + i] = (uint8_t)(crc >> (i * 8));

    fwrite(hdr, 1, hlen, file);
    if (dlen > 0) fwrite(data, 1, dlen, file);
    st->seqno++;
}

/* Write Opus ID header */
static void ogg_write_opus_id_header(ogg_state_t *st, FILE *file,
                                     uint8_t channels, uint32_t sample_rate, uint16_t pre_skip)
{
    uint8_t id_hdr[19];
    memcpy(id_hdr, "OpusHead", 8);
    id_hdr[8] = 1;  // version
    id_hdr[9] = channels;
    id_hdr[10] = pre_skip & 0xff;
    id_hdr[11] = (pre_skip >> 8) & 0xff;
    for (int i = 0; i < 4; i++)
        id_hdr[12 + i] = (sample_rate >> (i * 8)) & 0xff;
    id_hdr[16] = 0;  // output gain
    id_hdr[17] = 0;
    id_hdr[18] = 0;  // channel mapping family
    ogg_write_page(st, file, 0x02, 0, id_hdr, 19);
}

/* Write Opus comment header */
static void ogg_write_opus_comment_header(ogg_state_t *st, FILE *file)
{
    uint8_t comment[20];
    memcpy(comment, "OpusTags", 8);
    memset(comment + 8, 0, 12);
    ogg_write_page(st, file, 0x00, 0, comment, 20);
}

typedef struct {
    FILE *file;
    audio_format_t fmt;
    ogg_state_t ogg;
    int64_t granule;
    void *encoder;  // GMF Opus encoder (TODO: implement)
} opus_writer_t;

static esp_err_t opus_open(format_writer_handle_t handle,
                            FILE *file,
                            const audio_format_t *fmt)
{
    opus_writer_t *w = (opus_writer_t *)handle;
    w->file = file;
    w->fmt = *fmt;
    w->ogg.serial = 0x12345678;
    w->ogg.seqno = 0;
    w->granule = 0;
    w->encoder = NULL;

    ogg_crc_init();

    // Write Opus headers
    uint16_t pre_skip = 312;  // Opus encoder delay
    ogg_write_opus_id_header(&w->ogg, file, fmt->channels, fmt->sample_rate, pre_skip);
    ogg_write_opus_comment_header(&w->ogg, file);

    // TODO: Initialize GMF Opus encoder

    return ESP_OK;
}

static esp_err_t opus_write(format_writer_handle_t handle,
                             const void *data, size_t size)
{
    opus_writer_t *w = (opus_writer_t *)handle;

    // TODO: Encode PCM to Opus using GMF encoder
    // For now, this is a placeholder that assumes pre-encoded Opus data
    // In a full implementation, this would:
    // 1. Feed PCM data to GMF Opus encoder
    // 2. Get encoded Opus frames
    // 3. Write each frame as an OGG page

    // Simplified: write raw data as OGG page (not functional for PCM input)
    w->granule += 960;  // 20ms frame at 48kHz
    ogg_write_page(&w->ogg, w->file, 0x00, w->granule, data, size);

    return ESP_OK;
}

static esp_err_t opus_close(format_writer_handle_t handle)
{
    opus_writer_t *w = (opus_writer_t *)handle;

    // Write EOS page
    ogg_write_page(&w->ogg, w->file, 0x04, w->granule, NULL, 0);

    // TODO: Cleanup GMF encoder

    return ESP_OK;
}

static const format_writer_ops_t opus_ops = {
    .open = opus_open,
    .write = opus_write,
    .close = opus_close,
};

const format_writer_ops_t *get_opus_writer_ops(void)
{
    return &opus_ops;
}
