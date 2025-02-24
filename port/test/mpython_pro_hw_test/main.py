# from mpython import *

# '''
# 1、按键功能测试
# 说明：按A键，终端打印A，B键，打印B
# '''

# # button A
# from mpython import *

# def on_button_a_pressed(_):
#     print('A')

# button_a.event_pressed = on_button_a_pressed

# # button B
# def on_button_b_pressed(_):
#     print('B')

# button_b.event_pressed = on_button_b_pressed

# # #######################################################################

# '''
# 2、6轴
# 说明：打印6轴数据
# '''
# from mpython import *
# import time

# while True:
#     print('加速度 x:' + str(accelerometer.get_x()))
#     print('加速度 y:' + str(accelerometer.get_y()))
#     print('加速度 z:' + str(accelerometer.get_z()))
#     print('角速度 x:' + str(gyroscope.get_x()))
#     print('角速度 y:' + str(gyroscope.get_y()))
#     print('角速度 z:' + str(gyroscope.get_z()))
#     print('-------------------------------------------------------')
#     time.sleep(1)
    
# #########################################################################

# '''
# 3、磁力计
# 说明：打印磁力计3轴数据
# '''
# from mpython import *
# import time

# while True:
#     print('磁力计 x:' + str(magnetic.get_x()))
#     print('磁力计 y:' + str(magnetic.get_y()))
#     print('磁力计 z:' + str(magnetic.get_z()))
#     print('-------------------------------------------------------')
#     time.sleep(1)
  
# #########################################################################
# '''
# 4、声音触发器
# 说明：打印声音采样值
# '''
# from mpython import *
# import time

# while True:
#     print(sound.read())
#     time.sleep_ms(50)
    
# #########################################################################
# '''
# 5、触摸按键
# 说明：打印触摸按键值
# '''
# from mpython import *

# def on_touchpad_p_pressed(_):
#     print('P')

# touchpad_p.event_pressed = on_touchpad_p_pressed

# def on_touchpad_o_pressed(_):
#     print('O')

# touchpad_o.event_pressed = on_touchpad_o_pressed

# def on_touchpad_y_pressed(_):
#     print('Y')

# touchpad_y.event_pressed = on_touchpad_y_pressed

# def on_touchpad_n_pressed(_):
#     print('N')

# touchpad_n.event_pressed = on_touchpad_n_pressed

# def on_touchpad_t_pressed(_):
#     print('T')

# touchpad_t.event_pressed = on_touchpad_t_pressed

# def on_touchpad_h_pressed(_):
#     print('H')

# touchpad_h.event_pressed = on_touchpad_h_pressed

# '''
# 6、金手指IO
# 说明：外接LED模块，用代码测试每一个IO，效果闪灯。
# '''
# from mpython import *

# p0 = MPythonPin(0, PinMode.OUT)

# import time
# while True:
#     p0.write_digital(1)
#     time.sleep(0.1)
#     p0.write_digital(0)
#     time.sleep(0.1)
    
'''
7、
'''
    
'''
TFT LCD
'''
import lvgl_esp32
import os

# Adapt these values for your own configuration
spi = lvgl_esp32.SPI(
    2,
    baudrate=80_000_000,
    sck=36,
    mosi=37,
    miso=8,
)
spi.init()

display = lvgl_esp32.Display(
    spi=spi,
    width=320,
    height=172,
    gap_x=0,
    gap_y=34,
    swap_xy=True,
    mirror_x=True,
    mirror_y=True,
    invert= False,
    bgr=True,
    reset=7,
    dc=35,
    cs=34,
    pixel_clock=20_000_000,
)
display.init()

import lvgl as lv
import lvgl_esp32
from machine import Pin

bl = Pin(33, mode = Pin.OUT, pull = Pin.PULL_UP)
bl.value(1)

wrapper = lvgl_esp32.Wrapper(display)
wrapper.init()

from lv_utils import event_loop

screen = lv.screen_active()
screen.set_style_bg_color(lv.color_hex(0x000000), lv.PART.MAIN)

label = lv.label(screen)
label.set_style_text_font(lv.font_siyuan_songti_16, 0)
label.set_text("蒋朝辉 中华人民共和国")
label.set_style_text_color(lv.color_hex(0xffffff), lv.PART.MAIN)
label.align(lv.ALIGN.CENTER, 0, 0)

n = 0
def cb():  # 每40ms被调用
    # global n
    # n = n + 1
    # if n > 10000:
    #     n = 0
    # label.set_text(str(n))
    pass

event = event_loop(refresh_cb = cb)

# scr = lv.obj()
# scr.set_style_bg_color(lv.color_hex(0xff0000), lv.PART.MAIN)
# btn = lv.button(scr)
# btn.align(lv.ALIGN.TOP_LEFT, 20, 20)
# label = lv.label(btn)
# # label.set_style_text_font(lv.font_montserrat_24, 0)
# label.set_text('jiang zhao hui!')
# lv.screen_load(scr)

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

# while True:
#     lv.timer_handler_run_in_period(100)
