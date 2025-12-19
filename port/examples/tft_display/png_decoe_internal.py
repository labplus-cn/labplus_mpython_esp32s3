# 解码内置的png图片，图片保存在flash中， 以节省空间，腾出文件系统空间。内置png图片以索引号查询。
from mpython import *

tft_lcd.clear(lcd.BLACK)
w, h, buff = tft_lcd.decode_png_internal(0)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
while True:
    tft_lcd.blit(fb, 10, 10)
    tft_lcd.show()
    time.sleep_ms(50)
    tft_lcd.clear()
    tft_lcd.show()
    time.sleep_ms(50)