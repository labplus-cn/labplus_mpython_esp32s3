/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
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

// 包含必要的头文件
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/FreeRTOSConfig.h"

#include "sys/queue.h"
#include "esp_log.h"
#include "audio_event_iface.h"
#include "audio_error.h"
#include "audio_mem.h"

// 日志标签，用于调试输出
// 日志标签，用于调试输出
static const char *TAG = "AUDIO_EVT";

// 监听队列项结构体，用于管理监听的队列
typedef struct audio_event_iface_item {
    STAILQ_ENTRY(audio_event_iface_item)    next;        // 链表下一个节点
    QueueHandle_t                           queue;       // 队列句柄
    int                                     queue_size;  // 队列大小
    int                                     mark_to_remove; // 移除标记
} audio_event_iface_item_t;

// 监听队列链表头
typedef STAILQ_HEAD(audio_event_iface_list, audio_event_iface_item) audio_event_iface_list_t;

/**
 * Audio event structure
 * 音频事件接口主结构体
 */
struct audio_event_iface {
    QueueHandle_t               internal_queue;      // 内部队列，用于命令消息
    QueueHandle_t               external_queue;      // 外部队列，用于事件消息
    QueueSetHandle_t            queue_set;           // 队列集，用于处理多个队列
    int                         internal_queue_size; // 内部队列大小
    int                         external_queue_size; // 外部队列大小
    int                         queue_set_size;      // 队列集大小
    audio_event_iface_list_t    listening_queues;    // 监听队列链表
    void                        *context;            // 用户上下文指针
    on_event_iface_func         on_cmd;              // 命令回调函数
    int                         wait_time;           // 等待超时时间
    int                         type;                // 接口类型
};

// 初始化音频事件接口
audio_event_iface_handle_t audio_event_iface_init(audio_event_iface_cfg_t *config)
{
    // 分配内存给音频事件接口结构体
    audio_event_iface_handle_t evt = audio_calloc(1, sizeof(struct audio_event_iface));
    AUDIO_MEM_CHECK(TAG, evt, return NULL);
    // 设置配置参数
    evt->queue_set_size   = config->queue_set_size;
    evt->internal_queue_size = config->internal_queue_size;
    evt->external_queue_size = config->external_queue_size;
    evt->context = config->context;
    evt->on_cmd = config->on_cmd;
    evt->type = config->type;
    // 如果队列集大小大于0，创建队列集
    if (evt->queue_set_size) {
        evt->queue_set = xQueueCreateSet(evt->queue_set_size);
    }
    // 如果内部队列大小大于0，创建内部队列
    if (evt->internal_queue_size) {
        evt->internal_queue = xQueueCreate(evt->internal_queue_size, sizeof(audio_event_iface_msg_t));
        AUDIO_MEM_CHECK(TAG, evt->internal_queue, goto _event_iface_init_failed);
    }
    // 如果外部队列大小大于0，创建外部队列
    if (evt->external_queue_size) {
        evt->external_queue = xQueueCreate(evt->external_queue_size, sizeof(audio_event_iface_msg_t));
        AUDIO_MEM_CHECK(TAG, evt->external_queue, goto _event_iface_init_failed);
    } else {
        ESP_LOGD(TAG, "This emiiter have no queue set,%p", evt);  // 调试日志：无外部队列
    }

    // 初始化监听队列链表
    STAILQ_INIT(&evt->listening_queues);
    return evt;
_event_iface_init_failed:
    // 初始化失败时，清理已创建的队列
    if (evt->internal_queue) {
        vQueueDelete(evt->internal_queue);
    }
    if (evt->external_queue) {
        vQueueDelete(evt->external_queue);
    }
    return NULL;
}

// 清理监听器：清空队列集中所有队列并移除队列集
static esp_err_t audio_event_iface_cleanup_listener(audio_event_iface_handle_t listen)
{
    audio_event_iface_item_t *item, *tmp;
    //清空监听器中的所有队列（内部、外部和队列集）中的消息，确保没有残留消息影响后续操作。
    audio_event_iface_discard(listen); 
    //以下代码有点难解，但总体是遍历监听队列链表，清空每个队列中的消息，并从队列集中移除它们。
    STAILQ_FOREACH_SAFE(item, &listen->listening_queues, next, tmp) { //tmp用于安全遍历和删除
        audio_event_iface_msg_t dummy;
        while (audio_event_iface_read(listen, &dummy, 0) == ESP_OK);  // 清空listen队列集中活跃队列消息
        while (listen->queue_set && (xQueueRemoveFromSet(item->queue, listen->queue_set) != pdPASS)) {
            ESP_LOGW(TAG, "Error remove listener,%p", item->queue);  // 警告：移除失败
            while (audio_event_iface_read(listen, &dummy, 0) == ESP_OK);
        }
    }
    //上面删除队列集中的所有队列后，删除整个队列集
    if (listen->queue_set) {
        vQueueDelete(listen->queue_set);  // 删除队列集
        listen->queue_set = NULL;
    }
    return ESP_OK;
}

// 更新监听器：遍历监听器链表中队列，添加到重新创建的队列集
static esp_err_t audio_event_iface_update_listener(audio_event_iface_handle_t listen)
{
    audio_event_iface_item_t *item;
    int queue_size = 0;
    // 计算总队列大小
    STAILQ_FOREACH(item, &listen->listening_queues, next) {
        queue_size += item->queue_size;
    }
    if (queue_size) {
        listen->queue_set = xQueueCreateSet(queue_size);  // 创建新的队列集
    }
    STAILQ_FOREACH(item, &listen->listening_queues, next) {
        if (item->queue) {
            audio_event_iface_msg_t dummy;
            while (xQueueReceive(item->queue, &dummy, 0) == pdTRUE);  // 清空队列中消息
        }
        if (listen->queue_set && item->queue && xQueueAddToSet(item->queue, listen->queue_set) != pdPASS) {
            ESP_LOGE(TAG, "Error add queue items to queue set");  // 错误：添加失败
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

// 从队列集中读取消息
esp_err_t audio_event_iface_read(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg, TickType_t wait_time)
{
    if (evt->queue_set) {
        QueueSetMemberHandle_t active_queue;
        active_queue = xQueueSelectFromSet(evt->queue_set, wait_time);  // 选择活跃队列
        if (active_queue) {
            if (xQueueReceive(active_queue, msg, 0) == pdTRUE) {  // 从活跃队列接收消息
                return ESP_OK;
            }
        }
    }
    return ESP_FAIL;
}

// 销毁音频事件接口，清理所有资源
esp_err_t audio_event_iface_destroy(audio_event_iface_handle_t evt)
{
    audio_event_iface_cleanup_listener(evt);  // 清理监听器
    audio_event_iface_item_t *item, *tmp;
    STAILQ_FOREACH_SAFE(item, &evt->listening_queues, next, tmp) {
        STAILQ_REMOVE(&evt->listening_queues, item, audio_event_iface_item, next);  // 移除链表项
        audio_free(item);  // 释放内存
    }
    if (evt->internal_queue) {
        audio_event_iface_set_cmd_waiting_timeout(evt, 0);  // 设置超时为0
        vQueueDelete(evt->internal_queue);  // 删除内部队列
    }
    if (evt->external_queue) {
        vQueueDelete(evt->external_queue);  // 删除外部队列
    }
    if (evt->queue_set) {
        vQueueDelete(evt->queue_set);  // 删除队列集
    }
    audio_free(evt);  // 释放主结构体
    return ESP_OK;
}

// 设置外部事件监听器：将一个事件发布者的外部队列添加到监听器中。1.加入链表中，2.更新监听器队列集
esp_err_t audio_event_iface_set_listener(audio_event_iface_handle_t evt, audio_event_iface_handle_t listener)
{
    if ((NULL == evt->external_queue) || (0 == evt->external_queue_size)) {
        return ESP_ERR_INVALID_ARG;  // 外部队列是不存在
    }
    audio_event_iface_item_t *item = audio_calloc(1, sizeof(audio_event_iface_item_t));
    AUDIO_MEM_CHECK(TAG, item, return ESP_ERR_NO_MEM);

    if (audio_event_iface_cleanup_listener(listener) != ESP_OK) {
        AUDIO_ERROR(TAG, "Error cleanup listener");  // 错误：清理监听器失败
        return ESP_FAIL;
    }
    item->queue = evt->external_queue;  // 设置队列为外部队列
    item->queue_size = evt->external_queue_size;
    STAILQ_INSERT_TAIL(&listener->listening_queues, item, next);  // 添加到监听链表
    return audio_event_iface_update_listener(listener);  // 更新监听器
}

// 设置内部消息监听器：将一个事件发布者的内部队列添加到监听器中。1.加入链表中，2.更新监听器队列集
esp_err_t audio_event_iface_set_msg_listener(audio_event_iface_handle_t evt, audio_event_iface_handle_t listener)
{
    if ((NULL == evt->internal_queue) || (0 == evt->internal_queue_size)) {
        return ESP_ERR_INVALID_ARG;  // 检查内部队列是否存在
    }
    audio_event_iface_item_t *item = audio_calloc(1, sizeof(audio_event_iface_item_t));
    AUDIO_MEM_CHECK(TAG, item, return ESP_ERR_NO_MEM);
    if (audio_event_iface_cleanup_listener(listener) != ESP_OK) {
        AUDIO_ERROR(TAG, "Error cleanup listener");  // 错误：清理监听器失败
        return ESP_FAIL;
    }
    item->queue = evt->internal_queue;  // 设置队列为内部队列
    item->queue_size = evt->internal_queue_size;
    STAILQ_INSERT_TAIL(&listener->listening_queues, item, next);  // 添加到监听链表
    return audio_event_iface_update_listener(listener);  // 更新监听器
}

// 从一个监听者listen移除一个监听的事件发布者evt的内部或外部队列，取消对它的监听    
esp_err_t audio_event_iface_remove_listener(audio_event_iface_handle_t listen, audio_event_iface_handle_t evt)
{
    if ((NULL == evt->external_queue) || (0 == evt->external_queue_size)) {
        return ESP_ERR_INVALID_ARG;  // 检查外部队列是否存在
    }
    audio_event_iface_item_t *item, *tmp;
    if (audio_event_iface_cleanup_listener(listen) != ESP_OK) {
        return ESP_FAIL;
    }
    STAILQ_FOREACH_SAFE(item, &listen->listening_queues, next, tmp) {
        if (evt->external_queue == item->queue) { //队列名一致
            STAILQ_REMOVE(&listen->listening_queues, item, audio_event_iface_item, next);  // 移除匹配项
            audio_free(item);
        }
    }
    return audio_event_iface_update_listener(listen);  // 更新监听器
}

// 设置命令等待超时时间
esp_err_t audio_event_iface_set_cmd_waiting_timeout(audio_event_iface_handle_t evt, TickType_t wait_time)
{
    evt->wait_time = wait_time;
    return ESP_OK;
}

// 等待并处理内部命令消息
esp_err_t audio_event_iface_waiting_cmd_msg(audio_event_iface_handle_t evt)
{
    audio_event_iface_msg_t msg;
    if (evt->internal_queue && (xQueueReceive(evt->internal_queue, (void *)&msg, evt->wait_time) == pdTRUE)) {
        if (evt->on_cmd) {
            return evt->on_cmd((void *)&msg, evt->context);  // 调用命令回调
        }
    }
    return ESP_OK;
}

// 发送命令消息到内部队列
esp_err_t audio_event_iface_cmd(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg)
{
    if (evt->internal_queue && (xQueueSend(evt->internal_queue, (void *)msg, 0) != pdPASS)) {
        ESP_LOGW(TAG, "There are no space to dispatch queue");  // 警告：队列满
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 从中断服务例程发送命令消息（ISR 安全版本）
esp_err_t audio_event_iface_cmd_from_isr(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg)
{
    if (evt->internal_queue && (xQueueSendFromISR(evt->internal_queue, (void *)msg, 0) != pdPASS)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 发送事件消息到外部队列
esp_err_t audio_event_iface_sendout(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg)
{
    if (evt->external_queue) {
        if (xQueueSend(evt->external_queue, (void *)msg, 0) != pdPASS) {
            ESP_LOGW(TAG, "There is no space in external queue");  // 警告：队列满
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

// 清空所有队列中的消息
esp_err_t audio_event_iface_discard(audio_event_iface_handle_t evt)
{
    audio_event_iface_msg_t msg;
    if (evt->external_queue && evt->external_queue_size) {
        while (xQueueReceive(evt->external_queue, &msg, 0) == pdTRUE);  // 清空外部队列
    }
    if (evt->internal_queue && evt->internal_queue_size) {
        while (xQueueReceive(evt->internal_queue, &msg, 0) == pdTRUE);  // 清空内部队列
    }
    if (evt->queue_set && evt->queue_set_size) {
        while (audio_event_iface_read(evt, &msg, 0) == ESP_OK);  // 清空队列集
    }
    return ESP_OK;
}

// 监听消息
esp_err_t audio_event_iface_listen(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg, TickType_t wait_time)
{
    if (!evt) {
        return ESP_FAIL;
    }
    if (audio_event_iface_read(evt, msg, wait_time) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 获取外部队列句柄
QueueHandle_t audio_event_iface_get_queue_handle(audio_event_iface_handle_t evt)
{
    if (!evt) {
        return NULL;
    }
    return evt->external_queue;
}

// 获取内部队列句柄
QueueHandle_t audio_event_iface_get_msg_queue_handle(audio_event_iface_handle_t evt)
{
    if (!evt) {
        return NULL;
    }
    return evt->internal_queue;
}
