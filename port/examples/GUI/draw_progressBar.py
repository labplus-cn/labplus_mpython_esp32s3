from mpython import *

gui = UI()

display.clear(lcd.BLACK)
# 位置(10,30),长180，宽20，进度条位置20%，颜色红色
gui.ProgressBar(10, 30, 180, 20, 20, lcd.RED)
# 位置(200,0),长160，宽20，进度条位置50%，颜色蓝色
gui.stripBar(200, 0, 20, 160, 50, dir=0, color=lcd.BLUE)
display.show()