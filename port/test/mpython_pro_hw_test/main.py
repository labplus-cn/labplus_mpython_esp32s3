import lvgl_esp32
import os

# Adapt these values for your own configuration
spi = lvgl_esp32.SPI(
    2,
    baudrate=80_000_000,
    sck=21,
    mosi=47,
    miso=8,
)
spi.init()

display = lvgl_esp32.Display(
    spi=spi,
    width=240,
    height=240,
    gap_x=0,
    gap_y=0,
    swap_xy=False,
    mirror_x=False,
    mirror_y=False,
    invert=True,
    bgr=False,
    reset=42,
    dc=43,
    cs=44,
    pixel_clock=20_000_000,
)
display.init()

import lvgl as lv
import lvgl_esp32
from machine import Pin

bl = Pin(48, mode = Pin.OUT, pull = Pin.PULL_UP)
bl.value(0)

wrapper = lvgl_esp32.Wrapper(display)
wrapper.init()

# screen = lv.screen_active()
# screen.set_style_bg_color(lv.color_hex(0xff0000), lv.PART.MAIN)

# label = lv.label(screen)
# label.set_text("Hello world from MicroPython")
# label.set_style_text_color(lv.color_hex(0xffffff), lv.PART.MAIN)
# label.align(lv.ALIGN.CENTER, 0, 0)

scr = lv.obj()
scr.set_style_bg_color(lv.color_hex(0xff0000), lv.PART.MAIN)
btn = lv.button(scr)
btn.align(lv.ALIGN.TOP_LEFT, 20, 20)
label = lv.label(btn)
label.set_style_text_font(lv.font_montserrat_24, 0)
label.set_text('jiang zhao hui!')
lv.screen_load(scr)

# a = lv.anim_t()
# a.init()
# a.set_var(label)
# a.set_values(10, 50)
# a.set_duration(1000)
# # a.set_playback_delay(100)
# a.set_playback_duration(300)
# a.set_repeat_delay(500)
# a.set_repeat_count(lv.ANIM_REPEAT_INFINITE)
# a.set_path_cb(lv.anim_t.path_ease_in_out)
# a.set_custom_exec_cb(lambda _, v: label.set_y(v))
# a.start()

while True:
    lv.timer_handler_run_in_period(100)
