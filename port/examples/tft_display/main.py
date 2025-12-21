# main.py -- put your code here!
# 解码内置的png图片，图片保存在flash中， 以节省空间，腾出文件系统空间。内置png图片以索引号查询。
from mpython import *
import time

tft_lcd.clear(lcd.BLACK)
tft_lcd.show()
w, h, buff = tft_lcd.decode_png_internal(0)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)

#刷屏测试
while True:
    start = time.ticks_us()
    tft_lcd.blit(fb, 0,0)
    # tft_lcd.clear(lcd.RED)
    tft_lcd.show()
    # time.sleep_ms(500)
    tft_lcd.clear(lcd.GREEN)
    tft_lcd.show()
    # time.sleep_ms(500)
    print(time.ticks_diff(time.ticks_us(), start))