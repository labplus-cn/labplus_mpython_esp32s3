/*
 * modxiaozhi.c - 小智AI语音会话 MicroPython 模块
 *
 * 向 MicroPython 层暴露小智AI语音会话功能。
 *
 * Python 用法示例:
 *
 *   import xiaozhi
 *
 *   def on_state(state):
 *       states = ["idle", "connecting", "listening", "speaking", "error"]
 *       print("State:", states[state])
 *
 *   def on_stt(text):
 *       print("You said:", text)
 *
 *   def on_tts(state, text):
 *       print("TTS:", state, text or "")
 *
 *   # 初始化
 *   xiaozhi.init("wss://api.tenclass.net/xiaozhi/v1/", "your-token")
 *
 *   # 注册回调（可选）
 *   xiaozhi.on_state(on_state)
 *   xiaozhi.on_stt(on_stt)
 *   xiaozhi.on_tts(on_tts)
 *
 *   # 启动语音会话
 *   xiaozhi.start()
 *
 *   # ... 等待 ...
 *
 *   # 中断说话（切回监听）
 *   xiaozhi.abort()
 *
 *   # 结束会话
 *   xiaozhi.stop()
 *
 *   # 查询状态
 *   print(xiaozhi.state())  # 0=idle 1=connecting 2=listening 3=speaking 4=error
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "py/runtime.h"
#include "py/objstr.h"
#include "py/mphal.h"
#include "py/gc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "xiaozhi_session.h"
#include "activate.h"
#include "rec_to_file.h"

#define TAG "modxiaozhi"

/* ====================================================
 * MicroPython 回调持久化（防止被 GC 回收）
 * ==================================================== */

static mp_obj_t s_on_state_cb    = mp_const_none;
static mp_obj_t s_on_stt_cb      = mp_const_none;
static mp_obj_t s_on_tts_cb      = mp_const_none;
static mp_obj_t s_on_llm_cb      = mp_const_none;
static mp_obj_t s_on_message_cb  = mp_const_none;
static mp_obj_t s_on_wakeup_cb   = mp_const_none;

/* ====================================================
 * 内部 C 回调 → 调度到 MicroPython 调度器
 *
 * 注意: C 回调可能从 FreeRTOS 任务中被调用，
 *       不能直接调用 MicroPython 函数。
 *       使用 mp_sched_schedule() 将调用排队到 MicroPython 线程。
 * ==================================================== */

/* 用于传递 state 的包装结构 */
typedef struct { mp_obj_t cb; mp_obj_t arg; } mp_cb_pair_t;

/* ====================================================
 * C 回调实现:
 * 使用全局缓冲区 + 标志位的轮询方式
 * 从 FreeRTOS 任务安全地传递数据到 MicroPython 层。
 * 用户需在主循环中定期调用 xiaozhi.poll() 触发 Python 回调。
 * ==================================================== */

/* 简单的 C -> Python 调度：通过全局变量传递最新值 */
/* 这是在 FreeRTOS 任务和 MicroPython 线程间传递数据的简化方式 */

/* 最新消息缓冲区（用于跨任务传递） */
#define MSG_BUF_SIZE 512
static char s_last_stt[MSG_BUF_SIZE]     = {0};
static char s_last_tts_state[32]         = {0};
static char s_last_tts_text[MSG_BUF_SIZE]= {0};
static char s_last_llm_emotion[64]       = {0};
static char s_last_llm_text[MSG_BUF_SIZE]= {0};
static char s_last_message[MSG_BUF_SIZE] = {0};
static volatile int s_last_state = 0;

/* FreeRTOS 通知标志（用于标记有新数据待处理） */
static volatile uint32_t s_cb_flags = 0;
#define FLAG_STATE   (1 << 0)
#define FLAG_STT     (1 << 1)
#define FLAG_TTS     (1 << 2)
#define FLAG_LLM     (1 << 3)
#define FLAG_MESSAGE (1 << 4)
#define FLAG_WAKEUP  (1 << 5)

/* 用于保护 s_last_* 的互斥 */
static SemaphoreHandle_t s_msg_mutex = NULL;

/* C 层回调 - 只保存数据，不调用 Python */
static void c_on_state_impl(xiaozhi_state_t state)
{
    s_last_state = (int)state;
    s_cb_flags |= FLAG_STATE;
}

static void c_on_stt_impl(const char *text)
{
    if (!text) return;
    if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
    strncpy(s_last_stt, text, MSG_BUF_SIZE - 1);
    s_last_stt[MSG_BUF_SIZE - 1] = '\0';
    if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
    s_cb_flags |= FLAG_STT;
}

static void c_on_tts_impl(const char *state, const char *text)
{
    if (!state) return;
    if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
    strncpy(s_last_tts_state, state, sizeof(s_last_tts_state) - 1);
    if (text) strncpy(s_last_tts_text, text, MSG_BUF_SIZE - 1);
    else s_last_tts_text[0] = '\0';
    if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
    s_cb_flags |= FLAG_TTS;
}

static void c_on_llm_impl(const char *emotion, const char *text)
{
    if (!emotion) return;
    if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
    strncpy(s_last_llm_emotion, emotion, sizeof(s_last_llm_emotion) - 1);
    if (text) strncpy(s_last_llm_text, text, MSG_BUF_SIZE - 1);
    else s_last_llm_text[0] = '\0';
    if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
    s_cb_flags |= FLAG_LLM;
}

static void c_on_message_impl(const char *json)
{
    if (!json) return;
    if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
    strncpy(s_last_message, json, MSG_BUF_SIZE - 1);
    if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
    s_cb_flags |= FLAG_MESSAGE;
}

static void c_on_wakeup_impl(void)
{
    s_cb_flags |= FLAG_WAKEUP;
}

/* ====================================================
 * MicroPython 模块函数
 * ==================================================== */

/**
 * xiaozhi.init(url, token, device_id=None, client_id=None, volume=80)
 *
 * 初始化小智会话。
 *
 * Args:
 *   url       (str): WebSocket URL
 *   token     (str): Bearer 令牌
 *   device_id (str, optional): 设备 ID（默认使用 WiFi MAC）
 *   client_id (str, optional): 客户端 UUID（默认自动生成）
 *   volume    (int, optional): 扬声器音量 0-100（默认 80）
 */
static mp_obj_t mp_xiaozhi_init(size_t n_args, const mp_obj_t *pos_args,
                                 mp_map_t *kw_args)
{
    enum { ARG_url, ARG_token, ARG_device_id, ARG_client_id, ARG_volume };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,       MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_token,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_device_id, MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_client_id, MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_volume,    MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 80} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args),
                     allowed_args, args);

    const char *url       = mp_obj_str_get_str(args[ARG_url].u_obj);
    const char *token     = mp_obj_str_get_str(args[ARG_token].u_obj);
    const char *device_id = (args[ARG_device_id].u_obj != mp_const_none)
                            ? mp_obj_str_get_str(args[ARG_device_id].u_obj) : NULL;
    const char *client_id = (args[ARG_client_id].u_obj != mp_const_none)
                            ? mp_obj_str_get_str(args[ARG_client_id].u_obj) : NULL;
    int volume = args[ARG_volume].u_int;

    /* 初始化互斥锁 */
    if (!s_msg_mutex) {
        s_msg_mutex = xSemaphoreCreateMutex();
    }

    xiaozhi_config_t config = {
        .url              = url,
        .token            = token,
        .device_id        = device_id,
        .client_id        = client_id,
        .frame_duration_ms= 60,
        .output_volume    = volume,
        .input_gain_db    = 35.0f,
    };

    esp_err_t err = xiaozhi_init(&config);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("xiaozhi_init failed: %d"), err);
    }

    /* 注册 C 层回调 */
    xiaozhi_set_on_state(c_on_state_impl);
    xiaozhi_set_on_stt(c_on_stt_impl);
    xiaozhi_set_on_tts_state(c_on_tts_impl);
    xiaozhi_set_on_llm(c_on_llm_impl);
    xiaozhi_set_on_message(c_on_message_impl);
    xiaozhi_set_on_wakeup(c_on_wakeup_impl);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mp_xiaozhi_init_obj, 2, mp_xiaozhi_init);

/**
 * xiaozhi.deinit()
 *
 * 释放小智会话资源。
 */
static mp_obj_t mp_xiaozhi_deinit(void)
{
    esp_err_t err = xiaozhi_deinit();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("xiaozhi_deinit failed: %d"), err);
    }
    /* 清空回调引用 */
    s_on_state_cb   = mp_const_none;
    s_on_stt_cb     = mp_const_none;
    s_on_tts_cb     = mp_const_none;
    s_on_llm_cb     = mp_const_none;
    s_on_message_cb = mp_const_none;
    s_on_wakeup_cb  = mp_const_none;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_deinit_obj, mp_xiaozhi_deinit);

/**
 * xiaozhi.start(wakeup_mode=False)
 *
 * 启动语音会话（连接服务器，完成握手）。
 * 此函数会阻塞直到握手完成或超时（最多 10 秒）。
 *
 * Args:
 *   wakeup_mode (bool, optional): 唤醒词模式（默认 False）
 *     False: 连接后直接进入 LISTENING（自动收音）
 *     True:  连接后进入 CONNECTED，等待 listen_start() 或 trigger_wakeup()
 */
static mp_obj_t mp_xiaozhi_start(size_t n_args, const mp_obj_t *pos_args,
                                  mp_map_t *kw_args)
{
    enum { ARG_wakeup_mode };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_wakeup_mode, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    xiaozhi_set_wakeup_mode(args[ARG_wakeup_mode].u_bool);

    esp_err_t err = xiaozhi_start();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("xiaozhi_start failed: %d"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mp_xiaozhi_start_obj, 0, mp_xiaozhi_start);

/**
 * xiaozhi.stop()
 *
 * 停止语音会话（断开连接）。
 */
static mp_obj_t mp_xiaozhi_stop(void)
{
    esp_err_t err = xiaozhi_stop();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("xiaozhi_stop failed: %d"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_stop_obj, mp_xiaozhi_stop);

/**
 * xiaozhi.abort()
 *
 * 中断当前 TTS 播放，切回监听状态。
 */
static mp_obj_t mp_xiaozhi_abort(void)
{
    esp_err_t err = xiaozhi_abort();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("xiaozhi_abort failed: %d"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_abort_obj, mp_xiaozhi_abort);

/**
 * xiaozhi.listen_start()
 *
 * 开始监听（发送语音）。
 * 可在 CONNECTED 状态下调用，切换为 LISTENING 并向服务器发送 listen start。
 * 也可在 SPEAKING 状态下调用（相当于先 abort 再 listen）。
 */
static mp_obj_t mp_xiaozhi_listen_start(void)
{
    esp_err_t err = xiaozhi_listen_start();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("xiaozhi_listen_start failed: %d"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_listen_start_obj, mp_xiaozhi_listen_start);

/**
 * xiaozhi.listen_stop()
 *
 * 停止监听，切换为 CONNECTED 状态。
 * 可在 LISTENING 状态下调用。
 */
static mp_obj_t mp_xiaozhi_listen_stop(void)
{
    esp_err_t err = xiaozhi_listen_stop();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("xiaozhi_listen_stop failed: %d"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_listen_stop_obj, mp_xiaozhi_listen_stop);

/**
 * xiaozhi.trigger_wakeup()
 *
 * 手动触发唤醒事件（等效于 AFE 检测到唤醒词）。
 * 会触发已注册的 on_wakeup 回调（通过 poll() 派发）。
 * 典型用法：按钮按下时调用，在 on_wakeup 回调里调用 listen_start()。
 */
static mp_obj_t mp_xiaozhi_trigger_wakeup(void)
{
    xiaozhi_trigger_wakeup();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_trigger_wakeup_obj, mp_xiaozhi_trigger_wakeup);

/**
 * xiaozhi.state() -> int
 *
 * 返回当前状态:
 *   0 = idle
 *   1 = connecting
 *   2 = listening
 *   3 = speaking
 *   4 = error
 */
static mp_obj_t mp_xiaozhi_get_state(void)
{
    return MP_OBJ_NEW_SMALL_INT((int)xiaozhi_get_state());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_get_state_obj, mp_xiaozhi_get_state);

/* ---- 回调注册函数 ---- */

/**
 * xiaozhi.on_state(callback)
 *
 * 注册状态变化回调。
 * callback(state: int) 在状态改变时被调用。
 *
 * 注意: 回调不是实时的，需通过 xiaozhi.poll() 轮询触发。
 */
static mp_obj_t mp_xiaozhi_on_state(mp_obj_t cb)
{
    s_on_state_cb = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_xiaozhi_on_state_obj, mp_xiaozhi_on_state);

/**
 * xiaozhi.on_stt(callback)
 *
 * 注册语音识别结果回调。
 * callback(text: str) 在识别到语音时被调用。
 */
static mp_obj_t mp_xiaozhi_on_stt(mp_obj_t cb)
{
    s_on_stt_cb = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_xiaozhi_on_stt_obj, mp_xiaozhi_on_stt);

/**
 * xiaozhi.on_tts(callback)
 *
 * 注册 TTS 状态回调。
 * callback(state: str, text: str) 在 TTS 状态变化时被调用。
 * state: "start" / "stop" / "sentence_start"
 * text: 仅 sentence_start 时有值，其他为 None
 */
static mp_obj_t mp_xiaozhi_on_tts(mp_obj_t cb)
{
    s_on_tts_cb = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_xiaozhi_on_tts_obj, mp_xiaozhi_on_tts);

/**
 * xiaozhi.on_llm(callback)
 *
 * 注册 LLM 情感回调。
 * callback(emotion: str, text: str) 在收到 LLM 消息时被调用。
 */
static mp_obj_t mp_xiaozhi_on_llm(mp_obj_t cb)
{
    s_on_llm_cb = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_xiaozhi_on_llm_obj, mp_xiaozhi_on_llm);

/**
 * xiaozhi.on_message(callback)
 *
 * 注册原始 JSON 消息回调（用于高级处理）。
 * callback(json_text: str) 在收到任何 JSON 消息时被调用。
 */
static mp_obj_t mp_xiaozhi_on_message(mp_obj_t cb)
{
    s_on_message_cb = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_xiaozhi_on_message_obj, mp_xiaozhi_on_message);

/**
 * xiaozhi.on_wakeup(callback)
 *
 * 注册唤醒词检测回调。
 * callback() 在检测到唤醒词时被调用（通过 poll() 触发）。
 */
static mp_obj_t mp_xiaozhi_on_wakeup(mp_obj_t cb)
{
    s_on_wakeup_cb = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_xiaozhi_on_wakeup_obj, mp_xiaozhi_on_wakeup);

/**
 * xiaozhi.poll()
 *
 * 轮询待处理的回调事件并触发 Python 回调。
 * 需要在主循环中定期调用（建议 50ms 间隔）。
 *
 * 返回值: True 如果处理了任何事件，否则 False。
 *
 * 示例:
 *   import utime
 *   while True:
 *       xiaozhi.poll()
 *       utime.sleep_ms(50)
 */
static mp_obj_t mp_xiaozhi_poll(void)
{
    if (!s_cb_flags) {
        return mp_const_false;
    }

    bool handled = false;
    uint32_t flags = s_cb_flags;
    s_cb_flags = 0;  /* 清除标志（可能丢失并发更新，但简化了实现） */

    if ((flags & FLAG_STATE) && s_on_state_cb != mp_const_none) {
        mp_obj_t args[1] = { MP_OBJ_NEW_SMALL_INT(s_last_state) };
        mp_call_function_n_kw(s_on_state_cb, 1, 0, args);
        handled = true;
    }

    if ((flags & FLAG_STT) && s_on_stt_cb != mp_const_none) {
        if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
        mp_obj_t text = mp_obj_new_str(s_last_stt, strlen(s_last_stt));
        if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
        mp_obj_t args[1] = { text };
        mp_call_function_n_kw(s_on_stt_cb, 1, 0, args);
        handled = true;
    }

    if ((flags & FLAG_TTS) && s_on_tts_cb != mp_const_none) {
        if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
        mp_obj_t state_str = mp_obj_new_str(s_last_tts_state, strlen(s_last_tts_state));
        mp_obj_t text_str  = (s_last_tts_text[0])
                             ? mp_obj_new_str(s_last_tts_text, strlen(s_last_tts_text))
                             : mp_const_none;
        if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
        mp_obj_t args[2] = { state_str, text_str };
        mp_call_function_n_kw(s_on_tts_cb, 2, 0, args);
        handled = true;
    }

    if ((flags & FLAG_LLM) && s_on_llm_cb != mp_const_none) {
        if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
        mp_obj_t emotion = mp_obj_new_str(s_last_llm_emotion, strlen(s_last_llm_emotion));
        mp_obj_t text    = (s_last_llm_text[0])
                           ? mp_obj_new_str(s_last_llm_text, strlen(s_last_llm_text))
                           : mp_const_none;
        if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
        mp_obj_t args[2] = { emotion, text };
        mp_call_function_n_kw(s_on_llm_cb, 2, 0, args);
        handled = true;
    }

    if ((flags & FLAG_MESSAGE) && s_on_message_cb != mp_const_none) {
        if (s_msg_mutex) xSemaphoreTake(s_msg_mutex, portMAX_DELAY);
        mp_obj_t msg = mp_obj_new_str(s_last_message, strlen(s_last_message));
        if (s_msg_mutex) xSemaphoreGive(s_msg_mutex);
        mp_obj_t args[1] = { msg };
        mp_call_function_n_kw(s_on_message_cb, 1, 0, args);
        handled = true;
    }

    if ((flags & FLAG_WAKEUP) && s_on_wakeup_cb != mp_const_none) {
        mp_call_function_n_kw(s_on_wakeup_cb, 0, 0, NULL);
        handled = true;
    }

    return handled ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_poll_obj, mp_xiaozhi_poll);

/* ====================================================
 * 录音测试
 * ==================================================== */

/**
 * xiaozhi.record_to_file(path, duration_ms)
 *
 * 将麦克风录音保存到文件，同步阻塞直到录完。
 *
 * 文件格式：每帧 [uint16_be size][opus_frame_data]，可用于验证
 * 录音 pipeline 是否正常工作（无需连接服务器）。
 *
 * 前置条件：audio.init() 已调用。不可与 xiaozhi.start() 同时运行。
 *
 * Args:
 *   path        (str): 目标文件路径，如 "/rec.opus"
 *   duration_ms (int): 录音时长（毫秒）
 *
 * 示例:
 *   audio.init()
 *   xiaozhi.record_to_file("/rec.opus", 5000)
 *   import os; print(os.stat("/rec.opus"))
 */
static mp_obj_t mp_xiaozhi_record_to_file(mp_obj_t path_obj, mp_obj_t ms_obj)
{
    const char *path = mp_obj_str_get_str(path_obj);
    uint32_t duration_ms = (uint32_t)mp_obj_get_int(ms_obj);

    esp_err_t err = rec_to_file(path, duration_ms);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("record_to_file failed: %d"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_xiaozhi_record_to_file_obj,
                                  mp_xiaozhi_record_to_file);

/* ====================================================
 * 激活相关功能
 * ==================================================== */

/**
 * C 层激活码回调 —— 直接调用 Python 函数。
 *
 * activate_run() 从 MicroPython 线程上下文中被调用（同步阻塞），
 * 因此此回调同样运行在 MicroPython 线程中，可以直接调用 Python 函数，
 * 无需经过 poll() 机制。
 */
static void c_on_activation_code(const char *code, const char *message, void *user_data)
{
    mp_obj_t cb = (mp_obj_t)user_data;
    if (cb == mp_const_none) return;
    mp_obj_t args[2] = {
        mp_obj_new_str(code,    strlen(code)),
        mp_obj_new_str(message, strlen(message)),
    };
    mp_call_function_n_kw(cb, 2, 0, args);
}

/**
 * xiaozhi.activate(ota_url, on_code=None) -> dict
 *
 * 执行设备激活流程（同步阻塞）：
 *   1. 将 ota_url 写入 NVS 供激活模块使用
 *   2. 向服务器进行版本检查（CheckVersion）
 *   3. 若设备未激活：调用 on_code(code, message) 通知显示激活码，
 *      然后轮询服务器直至激活完成或超时
 *   4. 将服务器下发的 WebSocket 配置写入 NVS（供 load_config() 读取）
 *
 * Args:
 *   ota_url (str): OTA / 激活服务器地址，如 "https://ota.example.com/api/"
 *   on_code (callable, optional): 获得激活码时的回调 on_code(code:str, message:str)
 *
 * 激活结果通过 ESP_LOGI 打印；激活完成后调用 xiaozhi.load_config() 获取连接配置。
 *
 * 注意：设备已激活时，流程很快结束；未激活时最多阻塞约 30 秒。
 *
 * 示例:
 *   def show_code(code, msg):
 *       print("Activate at: https://xiaozhi.me  Code:", code)
 *
 *   xiaozhi.activate("https://ota.tenclass.net/xiaozhi/v1/", on_code=show_code)
 *   cfg = xiaozhi.load_config()
 *   if cfg:
 *       xiaozhi.init(cfg["url"], cfg["token"])
 */
static mp_obj_t mp_xiaozhi_activate(size_t n_args, const mp_obj_t *pos_args,
                                     mp_map_t *kw_args)
{
    enum { ARG_ota_url, ARG_on_code };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_ota_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_on_code, MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *ota_url  = mp_obj_str_get_str(args[ARG_ota_url].u_obj);
    mp_obj_t    on_code  = args[ARG_on_code].u_obj;

    /* 初始化激活上下文 */
    activate_ctx_t *ctx = NULL;
    activate_init(&ctx);
    if (!ctx) {
        mp_raise_msg(&mp_type_MemoryError,
                     MP_ERROR_TEXT("activate: activate_init OOM"));
    }

    /* 运行激活流程（同步阻塞；运行于 MicroPython 线程，可直接调用 Python 回调） */
    activate_result_t result;
    memset(&result, 0, sizeof(result));
    activate_on_code_t code_cb = (on_code != mp_const_none) ? c_on_activation_code : NULL;
    activate_run(ctx, ota_url, &result, code_cb, (void *)on_code);

    activate_deinit(ctx);

    /* 调试输出激活结果 */
    ESP_LOGI(TAG, "activate done: activated=%d ws_cfg=%d new_fw=%d",
             result.activated, result.has_websocket_config, result.has_new_firmware);
    if (result.has_new_firmware) {
        ESP_LOGI(TAG, "  firmware: %s  url: %s",
                 result.firmware_version, result.firmware_url);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mp_xiaozhi_activate_obj, 1, mp_xiaozhi_activate);

/**
 * xiaozhi.load_config() -> dict or None
 *
 * 从 NVS 读取激活流程写入的 WebSocket 连接配置。
 *
 * Returns:
 *   dict: {"url": str, "token": str}  已配置时返回
 *   None:                              未配置（尚未激活）时返回
 *
 * 示例:
 *   cfg = xiaozhi.load_config()
 *   if cfg:
 *       xiaozhi.init(cfg["url"], cfg["token"])
 *       xiaozhi.start()
 */
static mp_obj_t mp_xiaozhi_load_config(void)
{
    nvs_handle_t h;
    if (nvs_open("websocket", NVS_READONLY, &h) != ESP_OK) {
        return mp_const_none;
    }

    char url[256]   = {0};
    char token[256] = {0};
    size_t url_len   = sizeof(url);
    size_t token_len = sizeof(token);
    nvs_get_str(h, "url",   url,   &url_len);
    nvs_get_str(h, "token", token, &token_len);
    nvs_close(h);

    if (url[0] == '\0' || token[0] == '\0') {
        return mp_const_none;
    }

    mp_obj_t dict = mp_obj_new_dict(2);
    mp_obj_dict_store(dict,
        mp_obj_new_str("url", 3),
        mp_obj_new_str(url, strlen(url)));
    mp_obj_dict_store(dict,
        mp_obj_new_str("token", 5),
        mp_obj_new_str(token, strlen(token)));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_xiaozhi_load_config_obj, mp_xiaozhi_load_config);

/* ====================================================
 * 状态常量
 * ==================================================== */

/* 模块全局字典 */
static const mp_rom_map_elem_t xiaozhi_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_ROM_QSTR(MP_QSTR_xiaozhi) },

    /* 功能函数 */
    { MP_ROM_QSTR(MP_QSTR_init),         MP_ROM_PTR(&mp_xiaozhi_init_obj)        },
    { MP_ROM_QSTR(MP_QSTR_deinit),       MP_ROM_PTR(&mp_xiaozhi_deinit_obj)      },
    { MP_ROM_QSTR(MP_QSTR_start),        MP_ROM_PTR(&mp_xiaozhi_start_obj)       },
    { MP_ROM_QSTR(MP_QSTR_stop),         MP_ROM_PTR(&mp_xiaozhi_stop_obj)        },
    { MP_ROM_QSTR(MP_QSTR_abort),           MP_ROM_PTR(&mp_xiaozhi_abort_obj)          },
    { MP_ROM_QSTR(MP_QSTR_listen_start),    MP_ROM_PTR(&mp_xiaozhi_listen_start_obj)   },
    { MP_ROM_QSTR(MP_QSTR_listen_stop),     MP_ROM_PTR(&mp_xiaozhi_listen_stop_obj)    },
    { MP_ROM_QSTR(MP_QSTR_trigger_wakeup),  MP_ROM_PTR(&mp_xiaozhi_trigger_wakeup_obj) },
    { MP_ROM_QSTR(MP_QSTR_state),        MP_ROM_PTR(&mp_xiaozhi_get_state_obj)   },
    { MP_ROM_QSTR(MP_QSTR_poll),         MP_ROM_PTR(&mp_xiaozhi_poll_obj)        },

    /* 激活 */
    { MP_ROM_QSTR(MP_QSTR_activate),         MP_ROM_PTR(&mp_xiaozhi_activate_obj)        },
    { MP_ROM_QSTR(MP_QSTR_load_config),      MP_ROM_PTR(&mp_xiaozhi_load_config_obj)     },

    /* 测试工具 */
    { MP_ROM_QSTR(MP_QSTR_record_to_file),   MP_ROM_PTR(&mp_xiaozhi_record_to_file_obj)  },

    /* 回调注册 */
    { MP_ROM_QSTR(MP_QSTR_on_state),   MP_ROM_PTR(&mp_xiaozhi_on_state_obj)  },
    { MP_ROM_QSTR(MP_QSTR_on_stt),     MP_ROM_PTR(&mp_xiaozhi_on_stt_obj)    },
    { MP_ROM_QSTR(MP_QSTR_on_tts),     MP_ROM_PTR(&mp_xiaozhi_on_tts_obj)    },
    { MP_ROM_QSTR(MP_QSTR_on_llm),     MP_ROM_PTR(&mp_xiaozhi_on_llm_obj)    },
    { MP_ROM_QSTR(MP_QSTR_on_message), MP_ROM_PTR(&mp_xiaozhi_on_message_obj)},
    { MP_ROM_QSTR(MP_QSTR_on_wakeup),  MP_ROM_PTR(&mp_xiaozhi_on_wakeup_obj) },

    /* 状态常量 */
    { MP_ROM_QSTR(MP_QSTR_STATE_IDLE),       MP_ROM_INT(XIAOZHI_STATE_IDLE)       },
    { MP_ROM_QSTR(MP_QSTR_STATE_CONNECTING), MP_ROM_INT(XIAOZHI_STATE_CONNECTING) },
    { MP_ROM_QSTR(MP_QSTR_STATE_LISTENING),  MP_ROM_INT(XIAOZHI_STATE_LISTENING)  },
    { MP_ROM_QSTR(MP_QSTR_STATE_SPEAKING),   MP_ROM_INT(XIAOZHI_STATE_SPEAKING)   },
    { MP_ROM_QSTR(MP_QSTR_STATE_ERROR),      MP_ROM_INT(XIAOZHI_STATE_ERROR)      },
    { MP_ROM_QSTR(MP_QSTR_STATE_CONNECTED),  MP_ROM_INT(XIAOZHI_STATE_CONNECTED)  },
};

static MP_DEFINE_CONST_DICT(xiaozhi_module_globals, xiaozhi_module_globals_table);

const mp_obj_module_t xiaozhi_module = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&xiaozhi_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_xiaozhi, xiaozhi_module);
