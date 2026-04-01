/*
 * wav_writer.c - WAV format writer implementation
 */

#include "format_writer.h"
#include <string.h>

typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data[4];
    uint32_t data_size;
} wav_header_t;

typedef struct {
    FILE *file;
    audio_format_t fmt;
    uint32_t data_written;
} wav_writer_t;

static esp_err_t wav_open(format_writer_handle_t handle,
                          FILE *file,
                          const audio_format_t *fmt)
{
    wav_writer_t *w = (wav_writer_t *)handle;
    w->file = file;
    w->fmt = *fmt;
    w->data_written = 0;

    // Write header with placeholder size
    wav_header_t hdr = {
        .riff = {'R','I','F','F'},
        .file_size = 0,  // Will update in close()
        .wave = {'W','A','V','E'},
        .fmt = {'f','m','t',' '},
        .fmt_size = 16,
        .audio_format = 1,
        .num_channels = fmt->channels,
        .sample_rate = fmt->sample_rate,
        .byte_rate = fmt->sample_rate * fmt->channels * (fmt->bits_per_sample / 8),
        .block_align = fmt->channels * (fmt->bits_per_sample / 8),
        .bits_per_sample = fmt->bits_per_sample,
        .data = {'d','a','t','a'},
        .data_size = 0,  // Will update in close()
    };

    fwrite(&hdr, sizeof(hdr), 1, file);
    return ESP_OK;
}

static esp_err_t wav_write(format_writer_handle_t handle,
                           const void *data, size_t size)
{
    wav_writer_t *w = (wav_writer_t *)handle;
    fwrite(data, 1, size, w->file);
    w->data_written += size;
    return ESP_OK;
}

static esp_err_t wav_close(format_writer_handle_t handle)
{
    wav_writer_t *w = (wav_writer_t *)handle;

    // Update header with actual sizes
    fseek(w->file, 4, SEEK_SET);
    uint32_t file_size = w->data_written + sizeof(wav_header_t) - 8;
    fwrite(&file_size, 4, 1, w->file);

    fseek(w->file, 40, SEEK_SET);
    fwrite(&w->data_written, 4, 1, w->file);

    return ESP_OK;
}

static const format_writer_ops_t wav_ops = {
    .open = wav_open,
    .write = wav_write,
    .close = wav_close,
};

const format_writer_ops_t *get_wav_writer_ops(void)
{
    return &wav_ops;
}
