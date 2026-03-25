#include "py/obj.h"
#include "py/runtime.h"
#include "esp_mn_speech_commands.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sr.h"

static mp_obj_dict_t *callback_dict = NULL;
static bool pending_clear = false;  // sr.update()后置true，下次sr.add()时自动先清除旧命令词

void sr_invoke_callback_for_id(int command_id) {
    if (callback_dict == NULL) {
        return;
    }
    mp_obj_t key = mp_obj_new_int(command_id);
    mp_map_t *map = mp_obj_dict_get_map(callback_dict);
    mp_map_elem_t *lookup = mp_map_lookup(map, key, MP_MAP_LOOKUP);
    if (lookup != NULL) {
        mp_obj_t callback = lookup->value;
        // 调用回调函数（无参数调用，如果需要参数可自行扩展）
        mp_call_function_0(callback);
    }
}

// MicroPython 绑定函数
static mp_obj_t mp_sr_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_wakeup, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_wakeup_word, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },  // 唤醒应答词
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 6000} },              // 超时时间
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *word = NULL;  // 默认唤醒词
    if (args[0].u_obj != mp_const_none) {
        word = mp_obj_str_get_str(args[0].u_obj);
    }

    uint16_t t = args[1].u_int;

    sr_init(word, t);
    // test();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mp_sr_init_obj, 0, mp_sr_init);

// 清空语音指令
static mp_obj_t mp_esp_mn_commands_clear(void) {
    esp_mn_commands_clear();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_esp_mn_commands_clear_obj, mp_esp_mn_commands_clear);

// 添加语音指令，参数：command_id（整数,不可为0），text（拼音字符串）
// 首次调用（即上次sr.update()之后的第一次sr.add()）会自动清除旧命令词
static mp_obj_t mp_esp_mn_commands_add(mp_obj_t command_id_obj, mp_obj_t text_obj) {
    if (pending_clear) {
        esp_mn_commands_clear();
        pending_clear = false;
    }
    int command_id = mp_obj_get_int(command_id_obj);
    const char *text = mp_obj_str_get_str(text_obj);
    esp_mn_commands_add(command_id, text);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_esp_mn_commands_add_obj, mp_esp_mn_commands_add);

// 应用更新，并标记下次sr.add()时需要先清除旧命令词
static mp_obj_t mp_esp_mn_commands_update(void) {
    wait_for_multinet_ready();  // 确保multinet已初始化，否则命令词不生效
    esp_mn_commands_update();
    pending_clear = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_esp_mn_commands_update_obj, mp_esp_mn_commands_update);

// Micropython接口：设置指定命令 id 对应的回调函数
static mp_obj_t mp_sr_set_callback(mp_obj_t command_id_obj, mp_obj_t callback_obj) {
    // if (!mp_obj_is_callable(callback_obj)) {
    //     mp_raise_TypeError("回调函数必须可调用");
    // }
    // // 如果字典还未初始化，则初始化一个容量为4的字典
    // if (callback_dict == NULL) {
    //     callback_dict = mp_obj_new_dict(4);
    // }
    // mp_obj_dict_store(callback_dict, command_id_obj, callback_obj);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_sr_set_callback_obj, mp_sr_set_callback);

static mp_obj_t mp_sr_get_latest_command_id(void) {
    int cmd_id = get_latest_command_id();
    reset_latest_command_id();
    return mp_obj_new_int(cmd_id);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_get_latest_command_id_obj, mp_sr_get_latest_command_id);

static mp_obj_t mp_sr_get_wakeup_flag(void) {
    int cmd_id = get_wakeup_flag();
    return mp_obj_new_int(cmd_id);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_get_wakeup_flag_obj, mp_sr_get_wakeup_flag);

// 触发唤醒
static mp_obj_t mp_sr_trigger_wakeup(void) {
    set_wakeup_flag();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_trigger_wakeup_obj, mp_sr_trigger_wakeup);

// // 触发唤醒
// static mp_obj_t mp_sr_trigger_wakeup(void) {
//     sr_trigger_wakeup();
//     return mp_const_none;
// }
// static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_trigger_wakeup_obj, mp_sr_trigger_wakeup);

// // 触发睡眠
// static mp_obj_t mp_sr_trigger_sleep(void) {
//     sr_trigger_sleep();
//     return mp_const_none;
// }
// static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_trigger_sleep_obj, mp_sr_trigger_sleep);

// // 设置保持唤醒状态
// static mp_obj_t mp_sr_keep_awake(mp_obj_t enable_obj) {
//     bool enable = mp_obj_is_true(enable_obj);
//     sr_keep_awake(enable);
//     return mp_const_none;
// }
// static MP_DEFINE_CONST_FUN_OBJ_1(mp_sr_keep_awake_obj, mp_sr_keep_awake);

// // 开始语音命令检测
// static mp_obj_t mp_sr_start_vcmd_detection(void) {
//     sr_start_vcmd_detection();
//     return mp_const_none;
// }
// static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_start_vcmd_detection_obj, mp_sr_start_vcmd_detection);

// // 取消语音命令检测
// static mp_obj_t mp_sr_cancel_vcmd_detection(void) {
//     sr_cancel_vcmd_detection();
//     return mp_const_none;
// }
// static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_cancel_vcmd_detection_obj, mp_sr_cancel_vcmd_detection);

// static mp_obj_t mp_sr_start_vad_record(void) {
//     start_vad_record();
//     return mp_const_none;
// }
// static MP_DEFINE_CONST_FUN_OBJ_0(mp_sr_start_vad_record_obj, mp_sr_start_vad_record);

static const mp_rom_map_elem_t sr_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sr) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&mp_sr_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),  MP_ROM_PTR(&mp_esp_mn_commands_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_add),    MP_ROM_PTR(&mp_esp_mn_commands_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&mp_esp_mn_commands_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_callback),     MP_ROM_PTR(&mp_sr_set_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_latest_id),    MP_ROM_PTR(&mp_sr_get_latest_command_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_wakeup_flag),    MP_ROM_PTR(&mp_sr_get_wakeup_flag_obj) },
    { MP_ROM_QSTR(MP_QSTR_trigger_wakeup),    MP_ROM_PTR(&mp_sr_trigger_wakeup_obj) },
    // { MP_ROM_QSTR(MP_QSTR_trigger_sleep),    MP_ROM_PTR(&mp_sr_trigger_sleep_obj) },
    // { MP_ROM_QSTR(MP_QSTR_keep_awake),    MP_ROM_PTR(&mp_sr_keep_awake_obj) },
    // { MP_ROM_QSTR(MP_QSTR_start_vcmd_detection),    MP_ROM_PTR(&mp_sr_start_vcmd_detection_obj) },
    // { MP_ROM_QSTR(MP_QSTR_cancel_vcmd_detection),    MP_ROM_PTR(&mp_sr_cancel_vcmd_detection_obj) },
    // { MP_ROM_QSTR(MP_QSTR_start_vad_record),    MP_ROM_PTR(&mp_sr_start_vad_record_obj) },
};
static MP_DEFINE_CONST_DICT(sr_module_globals, sr_module_globals_table);

const mp_obj_module_t sr_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&sr_module_globals,
};