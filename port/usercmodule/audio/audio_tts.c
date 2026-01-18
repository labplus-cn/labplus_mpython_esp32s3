/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "tts.h"

static mp_obj_t mp_tts_generate(mp_obj_t text_obj)
{
    const char* text = mp_obj_str_get_str(text_obj);
    tts_generate(text); 

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_tts_generate_obj, mp_tts_generate);

static const mp_rom_map_elem_t tts_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_tts) },
    { MP_ROM_QSTR(MP_QSTR_generate), MP_ROM_PTR(&mp_tts_generate_obj) },
};

static MP_DEFINE_CONST_DICT(tts_module_globals, tts_module_globals_table);

const mp_obj_module_t machine_tts_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tts_module_globals,
};

// MP_REGISTER_MODULE(MP_QSTR_tts, machine_tts_module);