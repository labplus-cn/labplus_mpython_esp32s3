#include "py/obj.h"
#include "py/runtime.h"
#include "sc.h"

// MicroPython 绑定函数
static mp_obj_t mp_sc_init(void) {
    sc_init();

    return mp_const_none;  // 播放完成后返回 None
}

static MP_DEFINE_CONST_FUN_OBJ_0(mp_sc_init_obj, mp_sc_init);


// 模块定义
static const mp_rom_map_elem_t sc_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sc) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&mp_sc_init_obj) },
};
static MP_DEFINE_CONST_DICT(sc_module_globals, sc_module_globals_table);

// 模块结构体
const mp_obj_module_t sc_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&sc_module_globals,
};

// 注册模块
MP_REGISTER_MODULE(MP_QSTR_sc, sc_module);
