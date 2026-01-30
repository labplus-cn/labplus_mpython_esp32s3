#include "esp_log.h"
#include "esp_timer.h"
#include "py/runtime.h"
#include "lvgl.h"
#include "py/obj.h"
#include "esp_lcd_panel_ops.h"
#include "esp_board_manager.h"
#include "dev_display_lcd.h"

typedef struct _displayer_t
{
    mp_obj_base_t base;
    dev_display_lcd_handles_t *disp_handle;

    size_t buf_size;
    uint16_t *buf1;
    uint16_t *buf2;

    lv_display_t *lv_display;
} displayer_t;

displayer_t *displayer;
static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    displayer_t *disp = (displayer_t *) lv_display_get_user_data(display);

    lv_draw_sw_rgb565_swap(data, disp->buf_size);
    esp_lcd_panel_draw_bitmap(disp->disp_handle->panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, (uint16_t *)data);
}

static void transfer_done_cb(void *user_data)
{
    displayer_t *disp = (displayer_t *) user_data;
    lv_disp_flush_ready(disp->lv_display);
}

static uint32_t tick_get_cb()
{
    return esp_timer_get_time() / 1000;
}

static mp_obj_t lv_displayer_init(void)
{
    if(!displayer){
        displayer = calloc(1, sizeof(displayer_t));

        esp_board_manager_get_device_handle("display_lcd", (void **)&displayer->disp_handle);
        dev_display_lcd_config_t *cfg = NULL;
        esp_board_device_get_config_by_handle(displayer->disp_handle, (void **)&cfg);

        if (!lv_is_initialized()){
            lv_init();
        }
    
        displayer->lv_display = lv_display_create(cfg->lcd_width, cfg->lcd_height);
    
        displayer->buf_size = cfg->lcd_width * cfg->lcd_height;
        displayer->buf1 = heap_caps_malloc(displayer->buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        assert(displayer->buf1);
        displayer->buf2 = heap_caps_malloc(displayer->buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        assert(displayer->buf2);
    
        lv_display_set_buffers(displayer->lv_display, displayer->buf1, displayer->buf2, displayer->buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
        displayer->disp_handle->transfer_done_cb = transfer_done_cb;
        displayer->disp_handle->transfer_done_user_data = (void *) displayer;
        lv_display_set_flush_cb(displayer->lv_display, flush_cb);
        lv_display_set_user_data(displayer->lv_display, displayer);
        lv_tick_set_cb(tick_get_cb);
    }

    return mp_obj_new_int_from_uint(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(lv_displayer_init_obj, lv_displayer_init);

static mp_obj_t lv_displayer_deinit(void)
{
    if(displayer){
        lv_tick_set_cb(NULL);
        displayer->disp_handle->transfer_done_cb = NULL;
        displayer->disp_handle->transfer_done_user_data = NULL;
    
        if (displayer->lv_display != NULL){
            lv_display_delete(displayer->lv_display);
            displayer->lv_display = NULL;
        }
    
        displayer->buf_size = 0;
        if (displayer->buf1 != NULL){
            heap_caps_free(displayer->buf1);
            displayer->buf1 = NULL;
        }
        if (displayer->buf2 != NULL){
            heap_caps_free(displayer->buf2);
            displayer->buf2 = NULL;
        }
    
        if (lv_is_initialized()){
            lv_deinit();
        }

        free(displayer);
    }

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

