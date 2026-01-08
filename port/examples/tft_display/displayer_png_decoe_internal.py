# 解码内置的png图片，图片保存在flash中， 以节省空间，腾出文件系统空间。内置png图片以索引号查询。
from mpython import *

display.clear(lcd.WHITE)
w, h, buff = display.decode_png_internal(70) # 解码索引号为70的内置图片
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565) # 用解码出的图片创建一个framebuf缓存对象
display.blit(fb, 0, 0) # 把fb缓存图片数据写入显示（0,0)位置，支持裁剪，超出显示区会截掉

w, h, buff = display.decode_png_internal(28)
fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)
display.blit(fb, 200, 0)

display.show()

# # 刷屏测试
# while True:
#     t1 = time.ticks_ms()
#     display.blit(fb, 10, 10)
#     display.show()
#     # time.sleep_ms(50)
#     display.clear()
#     display.show()
#     # time.sleep_ms(50)
#     print(time.ticks_diff(time.ticks_ms(), t1))