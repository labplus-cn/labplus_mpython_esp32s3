# 解码文件系统中的png文件，并显示。
from mpython import *

display.clear(lcd.GREEN)
img = Image()
# 测试文件放入文件系统，加载png文件，生成图片framebuffer对象
fb = img.load('1.png')
display.blit(fb, 0, 0) # framebuffer图片刷入显示frambuffer，超出范围会被裁剪。
display.show()