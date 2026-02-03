#include <string.h>
#include "py/obj.h"
#include "py/runtime.h"
#include "esp_tts.h"
#include "tts.h"


static mp_obj_t mp_model_init(void) {
    model_init();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_model_init_obj, mp_model_init);

static mp_obj_t mp_tts_generate(mp_obj_t text_obj) {
    const char* text = mp_obj_str_get_str(text_obj);
    text_to_speech(text);
    
    return mp_const_none;  // 播放完成后返回 None
}

static MP_DEFINE_CONST_FUN_OBJ_1(mp_tts_generate_obj, mp_tts_generate);

static const mp_rom_map_elem_t tts_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_tts) },
    { MP_ROM_QSTR(MP_QSTR_generate), MP_ROM_PTR(&mp_tts_generate_obj) },
    { MP_ROM_QSTR(MP_QSTR_model_init), MP_ROM_PTR(&mp_model_init_obj) },
};
static MP_DEFINE_CONST_DICT(tts_module_globals, tts_module_globals_table);

const mp_obj_module_t tts_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&tts_module_globals,
};

// MP_REGISTER_MODULE(MP_QSTR_tts, tts_module);