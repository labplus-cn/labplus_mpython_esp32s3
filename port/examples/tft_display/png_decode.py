# 解码文件系统中的png文件，并显示。
from mpython import *

tft_lcd.clear(lcd.BLACK)
img = Image()
fb = img.load('images/B&W/1.png') # 加载png文件，生成图片framebuffer对象
tft_lcd.blit(fb, 200, 80) # framebuffer图片刷入显示frambuffer，超出范围会被裁剪。
tft_lcd.show()