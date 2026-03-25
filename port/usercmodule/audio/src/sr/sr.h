#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void sr_init(const char *word, uint16_t timeout_ms);
// 全局变量：最新触发的命令 id（初始设为 0，表示无有效命令）
// extern volatile int latest_command_id;
// extern volatile int sc_stop_flag;

// 获取最新命令 id
int get_latest_command_id(void);
// 重置最新命令 id（例如置为 -1）
void reset_latest_command_id(void);
int get_wakeup_flag(void);
void set_wakeup_flag(void);
void wait_for_multinet_ready(void);  // 阻塞直到multinet初始化完成，sr.update()调用前需先调用此函数

// // 触发唤醒
// void sr_trigger_wakeup(void);
// // 触发睡眠
// void sr_trigger_sleep(void);
// // 设置保持唤醒状态
// void sr_keep_awake(bool enable);
// // 开始语音命令检测
// void sr_start_vcmd_detection(void);
// // 取消语音命令检测
// void sr_cancel_vcmd_detection(void);
// void start_vad_record(void);
// void test(void);
#ifdef __cplusplus
}
#endif
