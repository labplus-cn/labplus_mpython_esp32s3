# 猜拳没游戏，需要用到AI摄像头。

from mpython import *
import smartcamera_k230 as smartcamera
import time
import random
import music
import neopixel

smart_camera = smartcamera.SmartCameraK230(tx=Pin.P1, rx=Pin.P0)
smart_camera.model_init(4)

my_rgb = neopixel.NeoPixel(Pin(Pin.P4), n=24, bpp=3, timing=1)

def make_rainbow(_neopixel, _num, _bright, _offset):
    _rgb = ((255,0,0), (255,127,0), (255,255,0), (0,255,0), (0,255,255), (0,0,255), (136,0,255), (255,0,0))
    for i in range(_num):
        t = 7 * i / _num
        t0 = int(t)
        r = round((_rgb[t0][0] + (t-t0)*(_rgb[t0+1][0]-_rgb[t0][0]))*_bright)>>8
        g = round((_rgb[t0][1] + (t-t0)*(_rgb[t0+1][1]-_rgb[t0][1]))*_bright)>>8
        b = round((_rgb[t0][2] + (t-t0)*(_rgb[t0+1][2]-_rgb[t0][2]))*_bright)>>8
        _neopixel[(i + _offset) % _num] = (r, g, b)

def Win_loss_record(_record):
    global i, flag, is_win, record, users, computer, j
    for i in range(3):
        if _record[i] == -1:
            display.fill_circle((240 + i * 30), 15, 10, lcd.BLACK)
        elif _record[i] == 1:
            display.fill_circle((240 + i * 30), 15, 10, lcd.BLUE)
        elif _record[i] == 2:
            display.fill_circle((240 + i * 30), 15, 10, lcd.RED)
        elif _record[i] == 3:
            display.fill_circle((240 + i * 30), 15, 10, lcd.GREEN)

random.seed(time.ticks_cpu())
display.clear(lcd.WHITE)
while True:
    display.clear(lcd.WHITE)
    display.DispChar('按下A键来和我猜拳吧', 0, 0, lcd.BLACK, False)
    w, h, buff = display.decode_png_internal(27)
    fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
    display.blit(fb, 60, 27)
    display.show()
    if button_a.is_pressed():
        flag = 0
        is_win = 0
        record = [-1, -1, -1]
        while True:
            smart_camera.hand_keypoint_class.recognize()
            display.clear(lcd.WHITE)
            display.DispChar('VS', 150, 73, lcd.BLACK, False)
            Win_loss_record(record)
            display.show()
            users = smart_camera.hand_keypoint_class.gesture_id
            if users != None:
                if not ((users == 0 or (users == 1 or users == 8))):
                    continue
                for count in range(5):
                    computer = random.randint(0, 2)
                    display.clear(lcd.WHITE)
                    if users == 0:
                        w, h, buff = display.decode_png_internal(55)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 39, 53)                    
                    elif users == 1:
                        w, h, buff = display.decode_png_internal(53)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 39, 53)
                    elif users == 8:
                        w, h, buff = display.decode_png_internal(54)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 39, 53)
                    display.DispChar('VS', 150, 73, lcd.BLACK, False)
                    Win_loss_record(record)
                    if computer == 0:
                        w, h, buff = display.decode_png_internal(55)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 216, 53)  
                    elif computer == 1:
                        w, h, buff = display.decode_png_internal(53)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 216, 53)  
                    elif computer == 2:
                        w, h, buff = display.decode_png_internal(54)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 216, 53)  
                    display.show()
                time.sleep(2)
                display.clear(lcd.WHITE)
                # 石头
                # 布
                # 剪刀
                if users == 0:
                    # 石头
                    # 布
                    # 剪刀
                    if computer == 0:
                        display.DispChar('平局', 136, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(40)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(255), int(153), int(0)))
                        rgb.write()
                        time.sleep_ms(1)
                        record[flag] = 3
                    elif computer == 1:
                        display.DispChar('你输了', 124, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(35)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(255), int(0), int(0)))
                        rgb.write()
                        time.sleep_ms(1)
                        record[flag] = 2
                    elif computer == 2:
                        display.DispChar('你赢啦', 124, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(31)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(51), int(255), int(51)))
                        rgb.write()
                        time.sleep_ms(1)
                        is_win = is_win + 1
                        record[flag] = 1
                elif users == 1:
                    # 石头
                    # 布
                    # 剪刀
                    if computer == 0:
                        display.DispChar('你赢啦', 124, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(31)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(51), int(255), int(51)))
                        rgb.write()
                        time.sleep_ms(1)
                        is_win = is_win + 1
                        record[flag] = 1
                    elif computer == 1:
                        display.DispChar('平局', 136, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(40)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(255), int(153), int(0)))
                        rgb.write()
                        time.sleep_ms(1)
                        record[flag] = 3
                    elif computer == 2:
                        display.DispChar('你输了', 124, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(35)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(255), int(0), int(0)))
                        rgb.write()
                        time.sleep_ms(1)
                        record[flag] = 2
                elif users == 8:
                    # 石头
                    # 布
                    # 剪刀
                    if computer == 0:
                        display.DispChar('你输了', 124, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(35)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(255), int(0), int(0)))
                        rgb.write()
                        time.sleep_ms(1)
                        record[flag] = 2
                    elif computer == 1:
                        display.DispChar('你赢啦', 124, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(31)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(51), int(255), int(51)))
                        rgb.write()
                        time.sleep_ms(1)
                        is_win = is_win + 1
                        record[flag] = 1
                    elif computer == 2:
                        display.DispChar('平局', 136, 0, lcd.BLACK, False)
                        w, h, buff = display.decode_png_internal(40)
                        fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
                        display.blit(fb, 60, 27) 
                        rgb.fill((int(255), int(153), int(0)))
                        rgb.write()
                        time.sleep_ms(1)
                        record[flag] = 3
                Win_loss_record(record)
                display.show()
                time.sleep(2)
                rgb.fill( (0, 0, 0) )
                rgb.write()
                time.sleep_ms(1)
                if not record[flag] == 3:
                    flag = flag + 1
                if flag == 3:
                    if is_win >= 2:
                        music.play(music.POWER_UP, pin=Pin.P12, wait=False, loop=False)
                        for j in range(0, 25, 2):
                            make_rainbow(my_rgb, 24, 50, j)
                            my_rgb.write()
                    else:
                        music.play(music.POWER_DOWN, pin=Pin.P12, wait=False, loop=False)
                        my_rgb.fill( (255, 0, 0) )
                        my_rgb.write()
                    time.sleep(2)
                    music.stop(Pin.P12)
                    my_rgb.fill( (0, 0, 0) )
                    my_rgb.write()
                    break
    time.sleep_ms(10)
