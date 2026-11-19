#include "stdio.h"
#include "stdlib.h"
#include "player.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wav_codec.h"
#include <sys/stat.h>
#include "bsp_audio.h"
#include "simple_file_decoder.h"
#include "simple_http_decoder.h"
#include "esp_audio_simple_dec.h"
#include "http_stream.h"

#define TAG    "player"
#define READ_RINGBUF_BLOCK_SIZE    (512)
#define READ_RINGBUF_BLOCK_NUM      (8)

player_handle_t *player = NULL;

static bool player_stop_and_wait(player_handle_t *instance)
{
    if (!instance || !instance->file_uri) {
        return true;
    }

    if (instance->player_state == PLAYER_STATE_PAUSE) {
        if (instance->simple_http_read_task_handle) {
            vTaskResume(instance->simple_http_read_task_handle);
        }
        if (instance->simple_http_decoder_task_handle) {
            vTaskResume(instance->simple_http_decoder_task_handle);
        }
    }

    if (instance->player_state != PLAYER_STATE_STOPPED) {
        xEventGroupSetBits(instance->player_event, EV_PLAY_STOP);
    }

    EventBits_t bits = xEventGroupWaitBits(
        instance->player_event,
        EV_PLAY_SESSION_DONE,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(PLAYER_STOP_TIMEOUT_MS));
    if ((bits & EV_PLAY_SESSION_DONE) == 0) {
        ESP_LOGE(TAG, "Timed out waiting for playback tasks to stop");
        return false;
    }
    return true;
}

void player_play(const char *uri)
{
    if (!uri) {
        ESP_LOGE(TAG, "Playback URI is null");
        return;
    }

    if (!player) {
        player = calloc(1, sizeof(player_handle_t));
        if (!player) {
            return;
        }

        player->player_event = xEventGroupCreate();
        player->decode_ringbuff = rb_create(READ_RINGBUF_BLOCK_SIZE, READ_RINGBUF_BLOCK_NUM);
        if (!player->player_event || !player->decode_ringbuff) {
            if (player->player_event) {
                vEventGroupDelete(player->player_event);
            }
            if (player->decode_ringbuff) {
                rb_destroy(player->decode_ringbuff);
            }
            free(player);
            player = NULL;
            return;
        }
    } else if (!player_stop_and_wait(player)) {
        return;
    }

    xEventGroupClearBits(player->player_event,
        EV_PLAY_STOP | EV_READ_END | EV_PLAY_COMPLETION_BITS);
    rb_reset(player->decode_ringbuff);
    player->file_uri = uri;
    player->player_state = PLAYER_STATE_PLAYING;

    if (strstr(uri, "http")) {
        BaseType_t result = xTaskCreatePinnedToCore(
            &simple_http_read_task, "simple_http_read_task", 4 * 1024,
            (void *)player, 8, &player->simple_http_read_task_handle, CORE_NUM1);
        if (result != pdPASS) {
            player->simple_http_read_task_handle = NULL;
            player_task_done(player,
                EV_PLAY_HTTP_READ_DONE | EV_PLAY_HTTP_DECODER_DONE);
        }
    } else {
        BaseType_t result = xTaskCreatePinnedToCore(
            &simple_file_decoder_task, "simple_file_decoder_task", 4 * 1024,
            (void *)player, 8, &player->simple_file_decoder_task_handle, CORE_NUM1);
        if (result != pdPASS) {
            player->simple_file_decoder_task_handle = NULL;
            player_task_done(player, EV_PLAY_FILE_DONE);
        }
    }
}

void player_pause(void)
{
    if (player && player->player_state == PLAYER_STATE_PLAYING) {
        if (player->simple_http_read_task_handle) {
            vTaskSuspend(player->simple_http_read_task_handle);
        }
        if (player->simple_http_decoder_task_handle) {
            vTaskSuspend(player->simple_http_decoder_task_handle);
        }
        player->player_state = PLAYER_STATE_PAUSE;
    }
}

void player_resume(void)
{
    if (player && player->player_state == PLAYER_STATE_PAUSE) {
        ESP_LOGE(TAG, "resume.");
        if (player->simple_http_read_task_handle) {
            vTaskResume(player->simple_http_read_task_handle);
        }
        if (player->simple_http_decoder_task_handle) {
            vTaskResume(player->simple_http_decoder_task_handle);
        }
        player->player_state = PLAYER_STATE_PLAYING;
    }
}

void player_stop(void)
{
    if (player) {
        player_stop_and_wait(player);
    }
}

void player_deinit(void)
{
    if (!player || !player_stop_and_wait(player)) {
        return;
    }

    player_handle_t *instance = player;
    player = NULL;
    if (instance->decode_ringbuff) {
        rb_destroy(instance->decode_ringbuff);
    }
    if (instance->player_queue) {
        vQueueDelete(instance->player_queue);
    }
    if (instance->player_event) {
        vEventGroupDelete(instance->player_event);
    }
    free(instance);
}

int player_get_state(void)
{
    return player ? player->player_state : PLAYER_STATE_INIT;
}

void player_task_done(player_handle_t *instance, EventBits_t done_bits)
{
    if (!instance || !instance->player_event) {
        return;
    }

    EventBits_t bits = xEventGroupSetBits(instance->player_event, done_bits);
    EventBits_t required_bits = (instance->file_uri && strstr(instance->file_uri, "http"))
        ? (EV_PLAY_HTTP_READ_DONE | EV_PLAY_HTTP_DECODER_DONE)
        : EV_PLAY_FILE_DONE;
    if ((bits & required_bits) == required_bits) {
        if (required_bits == (EV_PLAY_HTTP_READ_DONE | EV_PLAY_HTTP_DECODER_DONE)) {
            http_close();
        }
        instance->player_state = PLAYER_STATE_STOPPED;
        xEventGroupSetBits(instance->player_event, EV_PLAY_SESSION_DONE);
    }
}

void player_set_vol(int vol)
{
    bsp_audio_set_play_vol(vol);   
}

void player_increase_vol(void)
{
    int vol = 0;
    bsp_audio_get_play_vol(&vol);
    if (vol < 50) {
        vol += 3;
    } else if (vol < 70) {
        vol += 2;
    } else if (vol < 95) {
        vol += 1;
    } else {
        vol = 95;
    }
    bsp_audio_set_play_vol(vol);
}

void player_decrease_vol(void)
{
    int vol = 65;
    bsp_audio_get_play_vol(&vol);
    if (vol >= 95) {
        vol -= 1;
    } else if (vol >= 70) {
        vol -= 2;
    } else if (vol > 50) {
        vol -= 3;
    } else {
        vol = 50;
    }

    bsp_audio_set_play_vol(vol);
}
