#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "misc/lv_types.h"
#include "py/runtime.h"
#include "lvgl.h"
#include "py/obj.h"
#include "esp_lcd_panel_ops.h"
#include "esp_board_manager.h"
#include "dev_display_lcd.h"
#include <stdint.h>
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"

static const char *TAG = "mod_LV_DISP";
static lv_display_t *lvgl_disp = NULL;

static mp_obj_t lv_displayer_init(void)
{
    if(lvgl_disp == NULL){
        lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        lvgl_cfg.task_stack_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT;
        lvgl_port_init(&lvgl_cfg);

                /* Add LCD screen */
        ESP_LOGD(TAG, "Add LCD screen");
        dev_display_lcd_handles_t *disp_handle;
        esp_board_manager_get_device_handle("display_lcd", (void **)&disp_handle);
        dev_display_lcd_config_t *cfg = NULL;
        esp_board_device_get_config_by_handle(disp_handle, (void **)&cfg);

        const lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = disp_handle->io_handle,
            .panel_handle = disp_handle->panel_handle,
            .buffer_size = cfg->lcd_width * cfg->lcd_height * sizeof(uint16_t),
            .double_buffer = true,
            .hres = cfg->lcd_width,
            .vres = cfg->lcd_height,
            .monochrome = false,
            /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
            .rotation = {
                .swap_xy = false,
                .mirror_x = false,
                .mirror_y = false,
            },
            .flags = {
                .buff_dma = true,
                .swap_bytes = true,
            }
        };
        lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    }

    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(lv_displayer_init_obj, lv_displayer_init);

static mp_obj_t lv_displayer_deinit(void)
{
    lvgl_port_remove_disp(lvgl_disp);
    lvgl_port_deinit();

    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(lv_displayer_deinit_obj, lv_displayer_deinit);

static const mp_rom_map_elem_t lv_displayer_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_lv_displayer) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&lv_displayer_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&lv_displayer_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&lv_displayer_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&lv_displayer_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(lv_display_module_globals, lv_displayer_module_globals_table);

const mp_obj_module_t lv_display_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lv_display_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_lv_displayer, lv_display_cmodule);

