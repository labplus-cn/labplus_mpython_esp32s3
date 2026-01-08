# main.py -- put your code here!
# 解码内置的png图片，图片保存在flash中， 以节省空间，腾出文件系统空间。内置png图片以索引号查询。
from mpython import *
# import time

# display.clear(lcd.BLACK)
# display.show()
# w, h, buff = display.decode_png_internal(0)
# fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)

# #刷屏测试
# while True:
#     start = time.ticks_us()
#     display.blit(fb, 0,0)
#     # display.clear(lcd.RED)
#     display.show()
#     # time.sleep_ms(500)
#     display.clear(lcd.GREEN)
#     display.show()
#     # time.sleep_ms(500)
#     print(time.ticks_diff(time.ticks_us(), start))
from mpython import *
display.clear(lcd.GREEN)
import gc
# img = Image()
# 测试文件放入文件系统，加载png文件，生成图片framebuffer对象
i = 0
while True:
    # start = time.ticks_us()
    # if i < 10:
    #     fb = img.load(f'frame_0000{i}.png')
    # elif i > 10:
    #     fb = img.load(f'frame_000{i}.png')
    w, h, buff = display.decode_png_internal(i)
    fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
    display.blit(fb, 30, 30) # framebuffer图片刷入显示frambuffer，超出范围会被裁剪。
    display.show()
    # time.sleep_ms(10)
    # print(time.ticks_diff(time.ticks_us(), start))
    print(i)
    i = i+1
    if i >= 70:
        break
