/**
 * @file activate.h
 * @brief 设备认证激活流程接口
 */

#ifndef _ACTIVATE_H_
#define _ACTIVATE_H_

#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 激活结果结构体                                                         */
/* ------------------------------------------------------------------ */

/**
 * activate_run() 的输出结果。
 * 字段均在 activate_run() 返回后有效。
 */
typedef struct {
    bool activated;              /**< 激活成功 */
    bool has_activation_code;    /**< 响应中含激活码 */
    bool has_new_firmware;       /**< 有新固件可用 */
    bool has_mqtt_config;        /**< MQTT 配置已写入 NVS */
    bool has_websocket_config;   /**< WebSocket 配置已写入 NVS */
    bool has_server_time;        /**< 系统时间已同步 */

    char activation_code[32];    /**< 显示给用户的激活码 */
    char activation_message[128];/**< 服务器提示文字 */
    char firmware_version[32];   /**< 新固件版本号 */
    char firmware_url[256];      /**< 新固件下载 URL */
} activate_result_t;

/**
 * 激活码回调函数类型。
 *
 * 在服务器返回激活码、等待用户扫码/输入期间被调用。
 * 此时轮询尚未开始（约有 timeout_ms 时间窗口），适合向用户展示激活码。
 *
 * @param code      激活码字符串（如 "123456"）
 * @param message   服务器提示文字
 * @param user_data 调用方透传的指针
 */
typedef void (*activate_on_code_t)(const char *code, const char *message, void *user_data);

/* 前向声明（实现细节对外隐藏） */
typedef struct activate_ctx_t activate_ctx_t;

/* ------------------------------------------------------------------ */
/* 公共 API                                                              */
/* ------------------------------------------------------------------ */

/**
 * 初始化激活上下文（读取 efuse 序列号等）。
 * 必须在调用其他 activate_* 函数前调用。
 *
 * @param[out] out_ctx 输出激活上下文指针，失败时为 NULL
 */
void activate_init(activate_ctx_t **out_ctx);

/**
 * 释放激活上下文。
 *
 * @param ctx activate_init() 返回的上下文
 */
void activate_deinit(activate_ctx_t *ctx);

/**
 * 执行完整激活流程（同步阻塞）：
 *   1. 带退避重试的版本检查（CheckVersion）
 *   2. 若需要激活：调用 on_code 通知调用方显示激活码，然后轮询 Activate
 *   3. 将 MQTT / WebSocket 配置写入 NVS
 *
 * @param ctx        activate_init() 返回的上下文
 * @param out_result 输出结果，不可为 NULL
 * @param on_code    激活码回调（可为 NULL）；在轮询开始前被同步调用
 * @param user_data  透传给 on_code 的指针
 */
void activate_run(activate_ctx_t *ctx, const char *ota_url,
                  activate_result_t *out_result,
                  activate_on_code_t on_code, void *user_data);

/**
 * 在独立 FreeRTOS 任务（栈 8 KB，优先级 2）中启动激活流程。
 * result 的生命周期必须长于任务执行期间。
 *
 * @param ctx    激活上下文
 * @param result 输出结果缓冲区
 * @return pdPASS 任务创建成功，pdFAIL 创建失败
 */
BaseType_t activate_start_task(activate_ctx_t *ctx, activate_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* _ACTIVATE_H_ */
