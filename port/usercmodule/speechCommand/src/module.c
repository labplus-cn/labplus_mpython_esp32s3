#include "py/obj.h"
#include "py/runtime.h"
#include "sc.h"
#include "esp_mn_speech_commands.h"

// MicroPython 绑定函数
static mp_obj_t mp_sc_init(void) {
    sc_init();

    return mp_const_none;  // 播放完成后返回 None
}

static MP_DEFINE_CONST_FUN_OBJ_0(mp_sc_init_obj, mp_sc_init);

// 清空语音指令
static mp_obj_t mp_esp_mn_commands_clear(void) {
    esp_mn_commands_clear();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_esp_mn_commands_clear_obj, mp_esp_mn_commands_clear);

// 添加语音指令，参数：command_id（整数），text（拼音字符串）
static mp_obj_t mp_esp_mn_commands_add(mp_obj_t command_id_obj, mp_obj_t text_obj) {
    int command_id = mp_obj_get_int(command_id_obj);
    const char *text = mp_obj_str_get_str(text_obj);
    esp_mn_commands_add(command_id, text);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_esp_mn_commands_add_obj, mp_esp_mn_commands_add);

// 应用更新
static mp_obj_t mp_esp_mn_commands_update(void) {
    esp_mn_commands_update();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_esp_mn_commands_update_obj, mp_esp_mn_commands_update);

// 模块定义
static const mp_rom_map_elem_t sc_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sc) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&mp_sc_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),  MP_ROM_PTR(&mp_esp_mn_commands_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_add),    MP_ROM_PTR(&mp_esp_mn_commands_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&mp_esp_mn_commands_update_obj) },
};
static MP_DEFINE_CONST_DICT(sc_module_globals, sc_module_globals_table);

// 模块结构体
const mp_obj_module_t sc_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&sc_module_globals,
};

// 注册模块
MP_REGISTER_MODULE(MP_QSTR_sc, sc_module);
