from mpython import *

tft_lcd.clear()
# frame_colorbuf 绘图API测试
# 水平线和垂直线
tft_lcd.vline(10, 10, 152, lcd.BLUE)
tft_lcd.hline(10, 10, 300, lcd.BLUE)
tft_lcd.vline(310, 10, 152, lcd.BLUE)
tft_lcd.hline(10, 162, 300, lcd.BLUE)
# 任意两点线段
tft_lcd.line(0, 0, 320, 172, lcd.WHITE)
tft_lcd.line(320, 0, 0, 172, lcd.WHITE)
# 矩形和填充矩形
tft_lcd.rect(35, 35, 90, 100, lcd.WHITE)
tft_lcd.fill_rect(45, 50, 70, 70, lcd.RED)
# 圆和填充圆
tft_lcd.circle(160, 86, 35, lcd.WHITE)
tft_lcd.fill_circle(160, 86, 25, lcd.RED)     
# 三角形和填充三角形
tft_lcd.triangle(200, 50, 280, 50, 240, 130, lcd.WHITE)
tft_lcd.fill_triangle(210, 60, 270, 60, 240, 110, lcd.RED)
# 圆角矩形
tft_lcd.RoundRect(30, 21, 260, 140, 11, lcd.BLUE)

tft_lcd.show()
    