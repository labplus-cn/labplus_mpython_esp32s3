/**
 * @file activate.c
 * @brief 设备认证激活流程的 C 实现
 *
 * 实现流程：
 *   1. 读取设备序列号（efuse user_data）
 *   2. 向 OTA 服务器发起版本检查请求（CheckVersion）
 *      - 若响应包含 activation.code，显示激活码等待用户输入
 *      - 若响应包含 activation.challenge，进行 HMAC-SHA256 挑战验证
 *   3. 循环调用 Activate() 直到激活成功或超时
 *   4. 解析并保存 MQTT / WebSocket 配置到 NVS
 *   5. 若有新固件，触发 OTA 升级
 *
 * 参考规范：https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */

#include "activate.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_err.h>
// #include <esp_efuse.h>
// #include <esp_efuse_table.h>
#include <esp_app_desc.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>
#include <esp_http_client.h>
#include <sys/time.h>

#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

/* ------------------------------------------------------------------ */
/* 宏定义                                                               */
/* ------------------------------------------------------------------ */

#define TAG                 "Activate"
#define MAX_URL_LEN         256
#define MAX_BODY_LEN        4096
#define MAX_SERIAL_LEN      33
#define MAX_HMAC_HEX_LEN    65   /* 32 bytes * 2 hex chars + '\0' */
#define MAX_UUID_LEN        37
#define MAX_MAC_LEN         18
#define MAX_UA_LEN          128

#define ACTIVATE_MAX_RETRY       10
#define ACTIVATE_POLL_COUNT      10
#define ACTIVATE_POLL_DELAY_MS   3000
#define ACTIVATE_FAIL_DELAY_MS   10000
#define CHECK_VER_INIT_DELAY_S   10

/* NVS 命名空间 */
#define NVS_NS_WIFI      "wifi"
#define NVS_NS_MQTT      "mqtt"
#define NVS_NS_WEBSOCKET "websocket"
#define NVS_NS_BOARD     "board"

/* ------------------------------------------------------------------ */
/* 内部状态结构体                                                        */
/* ------------------------------------------------------------------ */

/* 与 activate.h 中的前向声明 `typedef struct activate_ctx_t activate_ctx_t` 匹配 */
struct activate_ctx_t {
    /* 设备标识 */
    char serial_number[MAX_SERIAL_LEN];
    bool has_serial_number;

    /* OTA / 激活服务器 URL（由调用方传入，不持久化） */
    char ota_url[MAX_URL_LEN];

    /* 版本信息 */
    char current_version[32];
    char firmware_version[32];
    char firmware_url[MAX_URL_LEN];
    bool has_new_version;

    /* 激活信息 */
    char activation_code[32];
    char activation_message[128];
    char activation_challenge[128];
    bool has_activation_code;
    bool has_activation_challenge;
    int  activation_timeout_ms;

    /* 协议配置标志 */
    bool has_mqtt_config;
    bool has_websocket_config;
    bool has_server_time;
};

/* ------------------------------------------------------------------ */
/* NVS 辅助函数                                                          */
/* ------------------------------------------------------------------ */

static esp_err_t nvs_set_str_if_changed(nvs_handle_t handle, const char *key, const char *value)
{
    char existing[256] = {0};
    size_t len = sizeof(existing);
    nvs_get_str(handle, key, existing, &len);
    if (strcmp(existing, value) != 0) {
        return nvs_set_str(handle, key, value);
    }
    return ESP_OK;
}

static esp_err_t nvs_set_i32_if_changed(nvs_handle_t handle, const char *key, int32_t value)
{
    int32_t existing = 0;
    nvs_get_i32(handle, key, &existing);
    if (existing != value) {
        return nvs_set_i32(handle, key, value);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 设备信息辅助                                                           */
/* ------------------------------------------------------------------ */

/** 获取 MAC 地址字符串，格式 "AA:BB:CC:DD:EE:FF" */
static void get_mac_address(char *buf, size_t buf_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, buf_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/** 从 NVS 获取或生成 UUID，格式标准 v4 */
static void get_board_uuid(char *buf, size_t buf_len)
{
    nvs_handle_t h;
    size_t len = buf_len;

    if (nvs_open(NVS_NS_BOARD, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_get_str(h, "uuid", buf, &len) == ESP_OK && buf[0] != '\0') {
            nvs_close(h);
            return;
        }
        /* 生成新 UUID v4 */
        uint32_t r[4];
        for (int i = 0; i < 4; i++) {
            r[i] = esp_random();
        }
        snprintf(buf, buf_len,
                 "%08lx-%04lx-4%03lx-%04lx-%012llx",
                 (unsigned long)(r[0]),
                 (unsigned long)(r[1] >> 16),
                 (unsigned long)(r[1] & 0x0FFF),
                 (unsigned long)((r[2] >> 16) & 0x3FFF) | 0x8000,
                 (unsigned long long)(((uint64_t)r[2] & 0xFFFF) << 32 | r[3]));
        nvs_set_str(h, "uuid", buf);
        nvs_commit(h);
        nvs_close(h);
    }
}

/** 获取 User-Agent 字符串 */
static void get_user_agent(char *buf, size_t buf_len)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    snprintf(buf, buf_len, "xiaozhi-esp32/%s (ESP32)", desc ? desc->version : "unknown");
}

/** 从 NVS wifi 命名空间读取 OTA URL */
static void get_ota_url(char *buf, size_t buf_len)
{
    nvs_handle_t h;
    size_t len = buf_len;
    buf[0] = '\0';
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, "ota_url", buf, &len);
        nvs_close(h);
    }
    if (buf[0] == '\0') {
        /* 回退到编译时配置 */
#ifdef CONFIG_OTA_URL
        strncpy(buf, CONFIG_OTA_URL, buf_len - 1);
        buf[buf_len - 1] = '\0';
#endif
    }
}

/* ------------------------------------------------------------------ */
/* HTTP 辅助                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} http_body_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_body_t *body = (http_body_t *)evt->user_data;
    if (!body) return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            size_t needed = body->len + evt->data_len + 1;
            if (needed > body->cap) {
                body->cap = needed * 2;
                body->buf = realloc(body->buf, body->cap);
            }
            if (body->buf) {
                memcpy(body->buf + body->len, evt->data, evt->data_len);
                body->len += evt->data_len;
                body->buf[body->len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 核心：CheckVersion                                                    */
/* ------------------------------------------------------------------ */

static esp_err_t check_version(activate_ctx_t *ctx)
{
    const char *url = ctx->ota_url;
    if (strlen(url) < 10) {
        ESP_LOGE(TAG, "OTA URL not configured");
        return ESP_ERR_INVALID_ARG;
    }

    const esp_app_desc_t *desc = esp_app_get_description();
    strncpy(ctx->current_version, desc ? desc->version : "0.0.0", sizeof(ctx->current_version) - 1);
    ESP_LOGI(TAG, "Current version: %s", ctx->current_version);

    /* 构建请求头 */
    char buf_mac[MAX_MAC_LEN], buf_uuid[MAX_UUID_LEN], buf_ua[MAX_UA_LEN];
    char headers[10][2];  /* 最多 10 个 header，每个 [key, value] */
    /* 由于 C 不支持 char[][2] 存放指针，改用结构体数组方式 */
    /* 使用固定长度 key/value 缓冲区 */
    typedef struct { char key[64]; char val[256]; } hdr_t;
    hdr_t hdrs[10];
    size_t n = 0;

    get_mac_address(buf_mac, sizeof(buf_mac));
    get_board_uuid(buf_uuid, sizeof(buf_uuid));
    get_user_agent(buf_ua, sizeof(buf_ua));

    strncpy(hdrs[n].key, "Activation-Version", 63); strncpy(hdrs[n].val, ctx->has_serial_number ? "2" : "1", 255); n++;
    strncpy(hdrs[n].key, "Device-Id",          63); strncpy(hdrs[n].val, buf_mac,  255); n++;
    strncpy(hdrs[n].key, "Client-Id",          63); strncpy(hdrs[n].val, buf_uuid, 255); n++;
    if (ctx->has_serial_number) {
        strncpy(hdrs[n].key, "Serial-Number",  63); strncpy(hdrs[n].val, ctx->serial_number, 255); n++;
    }
    strncpy(hdrs[n].key, "User-Agent",         63); strncpy(hdrs[n].val, buf_ua,   255); n++;
    strncpy(hdrs[n].key, "Accept-Language",    63); strncpy(hdrs[n].val, "zh-CN",  255); n++;
    strncpy(hdrs[n].key, "Content-Type",       63); strncpy(hdrs[n].val, "application/json", 255); n++;

    /* 构建请求体（系统信息 JSON，简化版） */
    char body[256];
    snprintf(body, sizeof(body),
             "{\"version\":\"%s\",\"mac\":\"%s\"}",
             ctx->current_version, buf_mac);

    /* 发起 HTTP 请求 */
    http_body_t rb = {NULL, 0, 0};
    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event_handler,
        .user_data     = &rb,
        .buffer_size   = 4096,
        .timeout_ms    = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    for (size_t i = 0; i < n; i++) {
        esp_http_client_set_header(client, hdrs[i].key, hdrs[i].val);
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t ret = esp_http_client_perform(client);
    int status = -1;
    if (ret == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);

    if (ret != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "CheckVersion failed: err=%s, status=%d", esp_err_to_name(ret), status);
        free(rb.buf);
        return (ret != ESP_OK) ? ret : ESP_FAIL;
    }

    /* 解析 JSON 响应 */
    if (!rb.buf) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *root = cJSON_Parse(rb.buf);
    free(rb.buf);

    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* --- activation --- */
    ctx->has_activation_code      = false;
    ctx->has_activation_challenge = false;
    ctx->activation_timeout_ms    = 30000;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        cJSON *msg = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(msg)) {
            strncpy(ctx->activation_message, msg->valuestring, sizeof(ctx->activation_message) - 1);
        }
        cJSON *code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            strncpy(ctx->activation_code, code->valuestring, sizeof(ctx->activation_code) - 1);
            ctx->has_activation_code = true;
        }
        cJSON *challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            strncpy(ctx->activation_challenge, challenge->valuestring, sizeof(ctx->activation_challenge) - 1);
            ctx->has_activation_challenge = true;
        }
        cJSON *timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            ctx->activation_timeout_ms = timeout_ms->valueint;
        }
    }

    /* --- mqtt 配置 --- */
    ctx->has_mqtt_config = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS_MQTT, NVS_READWRITE, &h) == ESP_OK) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, mqtt) {
                if (cJSON_IsString(item)) {
                    nvs_set_str_if_changed(h, item->string, item->valuestring);
                } else if (cJSON_IsNumber(item)) {
                    nvs_set_i32_if_changed(h, item->string, item->valueint);
                }
            }
            nvs_commit(h);
            nvs_close(h);
        }
        ctx->has_mqtt_config = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found");
    }

    /* --- websocket 配置 --- */
    ctx->has_websocket_config = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS_WEBSOCKET, NVS_READWRITE, &h) == ESP_OK) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, websocket) {
                if (cJSON_IsString(item)) {
                    nvs_set_str_if_changed(h, item->string, item->valuestring);
                } else if (cJSON_IsNumber(item)) {
                    nvs_set_i32_if_changed(h, item->string, item->valueint);
                }
            }
            nvs_commit(h);
            nvs_close(h);
        }
        ctx->has_websocket_config = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found");
    }

    /* --- server_time --- */
    ctx->has_server_time = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp      = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        if (cJSON_IsNumber(timestamp)) {
            double ts = timestamp->valuedouble;
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (double)(timezone_offset->valueint) * 60.0 * 1000.0;
            }
            struct timeval tv;
            tv.tv_sec  = (time_t)(ts / 1000.0);
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;
            settimeofday(&tv, NULL);
            ctx->has_server_time = true;
            ESP_LOGI(TAG, "System time updated: %lld", (long long)tv.tv_sec);
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found");
    }

    /* --- firmware --- */
    ctx->has_new_version = false;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        cJSON *fver  = cJSON_GetObjectItem(firmware, "version");
        cJSON *furl  = cJSON_GetObjectItem(firmware, "url");
        cJSON *force = cJSON_GetObjectItem(firmware, "force");

        if (cJSON_IsString(fver)) {
            strncpy(ctx->firmware_version, fver->valuestring, sizeof(ctx->firmware_version) - 1);
        }
        if (cJSON_IsString(furl)) {
            strncpy(ctx->firmware_url, furl->valuestring, sizeof(ctx->firmware_url) - 1);
        }
        if (cJSON_IsString(fver) && cJSON_IsString(furl)) {
            /* 简单版本号比较：比较三段数字 */
            int cur_maj = 0, cur_min = 0, cur_patch = 0;
            int new_maj = 0, new_min = 0, new_patch = 0;
            sscanf(ctx->current_version,  "%d.%d.%d", &cur_maj, &cur_min, &cur_patch);
            sscanf(ctx->firmware_version, "%d.%d.%d", &new_maj, &new_min, &new_patch);
            if (new_maj > cur_maj ||
                (new_maj == cur_maj && new_min > cur_min) ||
                (new_maj == cur_maj && new_min == cur_min && new_patch > cur_patch)) {
                ctx->has_new_version = true;
                ESP_LOGI(TAG, "New firmware available: %s -> %s",
                         ctx->current_version, ctx->firmware_version);
            }
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                ctx->has_new_version = true;
                ESP_LOGI(TAG, "Firmware upgrade forced by server");
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 核心：GetActivationPayload                                            */
/* ------------------------------------------------------------------ */

/**
 * 构建激活请求体 JSON。
 * 若设备有序列号，则计算 HMAC-SHA256 并附加；否则返回 "{}"。
 * @param ctx    激活上下文
 * @param buf    输出缓冲区
 * @param buf_sz 缓冲区大小
 */
static void get_activation_payload(const activate_ctx_t *ctx, char *buf, size_t buf_sz)
{
    if (!ctx->has_serial_number) {
        strncpy(buf, "{}", buf_sz - 1);
        buf[buf_sz - 1] = '\0';
        return;
    }

    char hmac_hex[MAX_HMAC_HEX_LEN] = {0};

#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32];
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0,
                                       (const uint8_t *)ctx->activation_challenge,
                                       strlen(ctx->activation_challenge),
                                       hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        strncpy(buf, "{}", buf_sz - 1);
        buf[buf_sz - 1] = '\0';
        return;
    }
    for (int i = 0; i < 32; i++) {
        snprintf(hmac_hex + i * 2, 3, "%02x", hmac_result[i]);
    }
#else
    ESP_LOGW(TAG, "HMAC not supported on this chip, hmac field will be empty");
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm",     "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", ctx->serial_number);
    cJSON_AddStringToObject(payload, "challenge",     ctx->activation_challenge);
    cJSON_AddStringToObject(payload, "hmac",          hmac_hex);

    char *json_str = cJSON_PrintUnformatted(payload);
    if (json_str) {
        strncpy(buf, json_str, buf_sz - 1);
        buf[buf_sz - 1] = '\0';
        cJSON_free(json_str);
    } else {
        strncpy(buf, "{}", buf_sz - 1);
    }
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", buf);
}

/* ------------------------------------------------------------------ */
/* 核心：Activate                                                        */
/* ------------------------------------------------------------------ */

/**
 * 向服务器提交激活请求（POST /activate）。
 * @return ESP_OK        激活成功 (HTTP 200)
 * @return ESP_ERR_TIMEOUT 服务器仍在处理 (HTTP 202)，需稍后重试
 * @return ESP_FAIL      激活失败
 */
static esp_err_t do_activate(const activate_ctx_t *ctx)
{
    if (!ctx->has_activation_challenge) {
        ESP_LOGW(TAG, "No activation challenge, skip Activate()");
        return ESP_FAIL;
    }

    /* 构建 activate URL */
    char url[MAX_URL_LEN];
    strncpy(url, ctx->ota_url, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    size_t url_len = strlen(url);
    if (url_len > 0 && url[url_len - 1] == '/') {
        strncat(url, "activate", sizeof(url) - url_len - 1);
    } else {
        strncat(url, "/activate", sizeof(url) - url_len - 1);
    }

    /* 构建请求体 */
    char body[512];
    get_activation_payload(ctx, body, sizeof(body));

    /* 构建请求头 */
    typedef struct { char key[64]; char val[256]; } hdr_t;
    hdr_t hdrs[10];
    size_t n = 0;
    char buf_mac[MAX_MAC_LEN], buf_uuid[MAX_UUID_LEN], buf_ua[MAX_UA_LEN];

    get_mac_address(buf_mac, sizeof(buf_mac));
    get_board_uuid(buf_uuid, sizeof(buf_uuid));
    get_user_agent(buf_ua, sizeof(buf_ua));

    strncpy(hdrs[n].key, "Activation-Version", 63); strncpy(hdrs[n].val, ctx->has_serial_number ? "2" : "1", 255); n++;
    strncpy(hdrs[n].key, "Device-Id",          63); strncpy(hdrs[n].val, buf_mac,  255); n++;
    strncpy(hdrs[n].key, "Client-Id",          63); strncpy(hdrs[n].val, buf_uuid, 255); n++;
    if (ctx->has_serial_number) {
        strncpy(hdrs[n].key, "Serial-Number",  63); strncpy(hdrs[n].val, ctx->serial_number, 255); n++;
    }
    strncpy(hdrs[n].key, "User-Agent",         63); strncpy(hdrs[n].val, buf_ua,   255); n++;
    strncpy(hdrs[n].key, "Accept-Language",    63); strncpy(hdrs[n].val, "zh-CN",  255); n++;
    strncpy(hdrs[n].key, "Content-Type",       63); strncpy(hdrs[n].val, "application/json", 255); n++;

    /* 发起 HTTP POST */
    http_body_t rb = {NULL, 0, 0};
    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event_handler,
        .user_data     = &rb,
        .buffer_size   = 512,
        .timeout_ms    = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_FAIL;
    }

    for (size_t i = 0; i < n; i++) {
        esp_http_client_set_header(client, hdrs[i].key, hdrs[i].val);
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t ret = esp_http_client_perform(client);
    int status = -1;
    if (ret == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }

    if (rb.buf) {
        if (status != 200) {
            ESP_LOGE(TAG, "Activate failed, status=%d, body=%s", status, rb.buf);
        }
        free(rb.buf);
    }
    esp_http_client_cleanup(client);

    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    if (status == 202) {
        return ESP_ERR_TIMEOUT;  /* 服务器处理中，需重试 */
    }
    if (status == 200) {
        ESP_LOGI(TAG, "Activation successful");
        return ESP_OK;
    }
    return ESP_FAIL;
}

/* ------------------------------------------------------------------ */
/* 公共接口                                                               */
/* ------------------------------------------------------------------ */

/**
 * 初始化激活上下文：读取 efuse 序列号。
 */
void activate_init(activate_ctx_t **out_ctx)
{
    activate_ctx_t *ctx = calloc(1, sizeof(activate_ctx_t));
    if (!ctx) {
        ESP_LOGE(TAG, "OOM: cannot allocate activate_ctx_t");
        *out_ctx = NULL;
        return;
    }

#ifdef ESP_EFUSE_BLOCK_USR_DATA
    uint8_t raw[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, raw, 32 * 8) == ESP_OK) {
        if (raw[0] != 0) {
            memcpy(ctx->serial_number, raw, 32);
            ctx->serial_number[32]   = '\0';
            ctx->has_serial_number   = true;
            ESP_LOGI(TAG, "Serial number: %s", ctx->serial_number);
        } else {
            ESP_LOGI(TAG, "No serial number in efuse");
        }
    }
#else
    ESP_LOGI(TAG, "efuse USER_DATA block not available on this chip");
#endif

    *out_ctx = ctx;
}

/**
 * 释放激活上下文。
 */
void activate_deinit(activate_ctx_t *ctx)
{
    free(ctx);
}

/**
 * 主激活任务入口（在独立 FreeRTOS 任务中调用）。
 *
 * 流程：
 *   1. 带退避重试地调用 check_version()
 *   2. 若有新固件，打印升级提示（OTA 升级需集成 Ota::Upgrade）
 *   3. 若需要激活，显示激活码并循环调用 do_activate()
 *   4. 完成后设置 out_result 标志
 */
void activate_run(activate_ctx_t *ctx, const char *ota_url,
                  activate_result_t *out_result,
                  activate_on_code_t on_code, void *user_data)
{
    if (!ctx || !out_result) return;
    if (ota_url) {
        strncpy(ctx->ota_url, ota_url, sizeof(ctx->ota_url) - 1);
        ctx->ota_url[sizeof(ctx->ota_url) - 1] = '\0';
    }
    memset(out_result, 0, sizeof(*out_result));

    const int MAX_RETRY  = ACTIVATE_MAX_RETRY;
    int retry_count      = 0;
    int retry_delay_s    = CHECK_VER_INIT_DELAY_S;

    /* ---- 阶段 1：版本检查（带退避重试） ---- */
    while (true) {
        ESP_LOGI(TAG, "Checking version... (attempt %d/%d)", retry_count + 1, MAX_RETRY);
        esp_err_t err = check_version(ctx);

        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, aborting version check");
                return;
            }
            ESP_LOGW(TAG, "Version check failed (err=0x%x), retry in %ds (%d/%d)",
                     err, retry_delay_s, retry_count, MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
            retry_delay_s *= 2;  /* 指数退避 */
            continue;
        }

        retry_count   = 0;
        retry_delay_s = CHECK_VER_INIT_DELAY_S;

        /* ---- 阶段 2：固件升级提示 ---- */
        if (ctx->has_new_version) {
            ESP_LOGI(TAG, "New firmware: %s  url: %s",
                     ctx->firmware_version, ctx->firmware_url);
            out_result->has_new_firmware = true;
            strncpy(out_result->firmware_version, ctx->firmware_version,
                    sizeof(out_result->firmware_version) - 1);
            strncpy(out_result->firmware_url, ctx->firmware_url,
                    sizeof(out_result->firmware_url) - 1);
            /* 实际 OTA 升级由上层（application）调用 esp_ota 完成 */
        }

        /* ---- 阶段 3：激活 ---- */
        if (!ctx->has_activation_code && !ctx->has_activation_challenge) {
            /* 已激活设备，直接退出循环 */
            break;
        }

        /* 显示激活码，并通知调用方 */
        if (ctx->has_activation_code) {
            ESP_LOGI(TAG, "=== ACTIVATION CODE: [%s] ===", ctx->activation_code);
            ESP_LOGI(TAG, "Message: %s", ctx->activation_message);
            out_result->has_activation_code = true;
            strncpy(out_result->activation_code,    ctx->activation_code,
                    sizeof(out_result->activation_code) - 1);
            strncpy(out_result->activation_message, ctx->activation_message,
                    sizeof(out_result->activation_message) - 1);
            /* 在轮询开始前同步调用回调，让调用方有机会向用户展示激活码 */
            if (on_code) {
                on_code(ctx->activation_code, ctx->activation_message, user_data);
            }
        }

        /* 轮询激活结果 */
        bool activated = false;
        for (int i = 0; i < ACTIVATE_POLL_COUNT; i++) {
            ESP_LOGI(TAG, "Activating... (%d/%d)", i + 1, ACTIVATE_POLL_COUNT);
            esp_err_t aerr = do_activate(ctx);
            if (aerr == ESP_OK) {
                activated = true;
                break;
            } else if (aerr == ESP_ERR_TIMEOUT) {
                /* 服务器仍在处理，等待后重试 */
                vTaskDelay(pdMS_TO_TICKS(ACTIVATE_POLL_DELAY_MS));
            } else {
                /* 激活失败，等待较长时间后重试 */
                vTaskDelay(pdMS_TO_TICKS(ACTIVATE_FAIL_DELAY_MS));
            }
        }

        if (activated) {
            out_result->activated = true;
            /* 激活成功后重新拉取版本信息（获取协议配置） */
            check_version(ctx);
            break;
        }

        /* 未能激活，重新检查版本（服务器可能更新了 challenge） */
        ESP_LOGW(TAG, "Activation not confirmed, re-checking version...");
    }

    /* ---- 阶段 4：协议配置 ---- */
    out_result->has_mqtt_config      = ctx->has_mqtt_config;
    out_result->has_websocket_config = ctx->has_websocket_config;
    out_result->has_server_time      = ctx->has_server_time;

    ESP_LOGI(TAG, "Activation flow complete. mqtt=%d, ws=%d, activated=%d",
             out_result->has_mqtt_config,
             out_result->has_websocket_config,
             out_result->activated);
}

/* ------------------------------------------------------------------ */
/* FreeRTOS 任务包装（可选）                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    activate_ctx_t    *ctx;
    activate_result_t *result;
} activation_task_arg_t;

static void activation_task_fn(void *arg)
{
    activation_task_arg_t *a = (activation_task_arg_t *)arg;
    activate_run(a->ctx, NULL, a->result, NULL, NULL);
    /* 通知调用方：可用 EventGroup 或队列，此处简化为日志 */
    ESP_LOGI(TAG, "Activation task finished");
    vTaskDelete(NULL);
}

/**
 * 在独立 FreeRTOS 任务中启动激活流程。
 * @param ctx    激活上下文（需已调用 activate_init）
 * @param result 输出结果（生命周期需长于任务执行期间）
 * @return pdPASS 任务创建成功
 */
BaseType_t activate_start_task(activate_ctx_t *ctx, activate_result_t *result)
{
    static activation_task_arg_t task_arg;
    task_arg.ctx    = ctx;
    task_arg.result = result;

    return xTaskCreate(activation_task_fn,
                       "activation",
                       4096 * 2,
                       &task_arg,
                       2,
                       NULL);
}
