/*
 * format_writer.h - Audio format writer interface
 *
 * Provides a unified interface for writing audio data in different formats
 * (WAV, OPUS, MP3, etc.) to LittleFS files using POSIX file operations.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct format_writer_s *format_writer_handle_t;

typedef struct {
    uint32_t sample_rate;     // Sample rate (e.g., 8000, 16000)
    uint16_t channels;        // Number of channels (1=mono, 2=stereo)
    uint16_t bits_per_sample; // Bits per sample (8, 16, 24, 32)
} audio_format_t;

/**
 * Format writer operations (vtable pattern)
 */
typedef struct {
    /**
     * Open file and write format header
     */
    esp_err_t (*open)(format_writer_handle_t handle,
                      FILE *file,
                      const audio_format_t *fmt);

    /**
     * Write audio data (PCM or encoded)
     */
    esp_err_t (*write)(format_writer_handle_t handle,
                       const void *data, size_t size);

    /**
     * Finalize and close file
     */
    esp_err_t (*close)(format_writer_handle_t handle);

} format_writer_ops_t;

/**
 * Get WAV format writer operations
 */
const format_writer_ops_t *get_wav_writer_ops(void);

/**
 * Get OPUS format writer operations
 */
const format_writer_ops_t *get_opus_writer_ops(void);

#ifdef __cplusplus
}
#endif
