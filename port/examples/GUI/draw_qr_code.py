from mpython import *

gui = UI()

display.clear(lcd.BLACK)
# 位置(100, 10)显示字字符串的二维码，显示比例设为4
gui.qr_code("Hello, mPython!", 100, 10, scale=4)
display.show()