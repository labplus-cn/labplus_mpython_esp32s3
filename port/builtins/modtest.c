#include "py/obj.h"
#include "py/runtime.h"
#include "esp_tts.h"
#include "tts_pcm.h"

#ifndef STATIC
#define STATIC static
#endif

// MicroPython 绑定函数
STATIC mp_obj_t mp_tts_generate(mp_obj_t text_obj) {
    // 获取文本参数
    const char* text = mp_obj_str_get_str(text_obj);

    // 直接调用 text_to_speech 播放音频
    text_to_speech(text);

    return mp_const_none;  // 播放完成后返回 None
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_tts_generate_obj, mp_tts_generate);


// 模块定义
STATIC const mp_rom_map_elem_t tts_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_tts) },
    { MP_ROM_QSTR(MP_QSTR_generate), MP_ROM_PTR(&mp_tts_generate_obj) },
};
STATIC MP_DEFINE_CONST_DICT(tts_module_globals, tts_module_globals_table);

// 模块结构体
const mp_obj_module_t tts_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&tts_module_globals,
};

// 注册模块
MP_REGISTER_MODULE(MP_QSTR_tts, tts_module);

