# 解码内置的png图片，图片保存在flash中， 以节省空间，腾出文件系统空间。内置png图片以索引号查询。
from mpython import *

display.clear(lcd.WHITE)

w, h, buff = display.decode_png_internal(71)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
display.blit(fb, 0, 0)

w, h, buff = display.decode_png_internal(64)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
display.blit(fb, 100, 0)

w, h, buff = display.decode_png_internal(65)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
display.blit(fb, 200, 0)

w, h, buff = display.decode_png_internal(66)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
display.blit(fb, 0, 85)

w, h, buff = display.decode_png_internal(61)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
display.blit(fb, 100, 85)

w, h, buff = display.decode_png_internal(62)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
display.blit(fb, 200, 85)

display.show()
while True:
    display.blit(fb, 10, 10)
    display.show()
    time.sleep_ms(50)
    display.clear()
    display.show()
    time.sleep_ms(50)