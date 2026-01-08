'''
display继承framebuf,framebuf实现以下功能
1、创建图形缓存区
2、图形绘制：点、线、圆...
3、png图片解码
4、清屏（背景填充）
5、内置文本显示
'''
from mpython import *

display.clear()
# frame_colorbuf 绘图API测试
# 水平线和垂直线
display.vline(10, 10, 152, lcd.BLUE)
display.hline(10, 10, 300, lcd.BLUE)
display.vline(310, 10, 152, lcd.BLUE)
display.hline(10, 162, 300, lcd.BLUE)
# 任意两点线段
display.line(0, 0, 320, 172, lcd.WHITE)
display.line(320, 0, 0, 172, lcd.WHITE)
# 矩形和填充矩形
display.rect(35, 35, 90, 100, lcd.WHITE)
display.fill_rect(45, 50, 70, 70, lcd.RED)
# 圆和填充圆
display.circle(160, 86, 35, lcd.WHITE)
display.fill_circle(160, 86, 25, lcd.RED)     
# 三角形和填充三角形
display.triangle(200, 50, 280, 50, 240, 130, lcd.WHITE)
display.fill_triangle(210, 60, 270, 60, 240, 110, lcd.RED)
# 圆角矩形
display.RoundRect(30, 21, 260, 140, 11, lcd.BLUE)

display.show()
    