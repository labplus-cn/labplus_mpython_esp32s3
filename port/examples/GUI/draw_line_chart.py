from mpython import *
import audio

# 构建一个图表，位置（40，40）
chart = MicroChart(x0 = 40, y0 = 40)
# 显示数据100个，图纸是二维数组，可以显示多组折线图，这里只显示了一组数据。
line_data = [[0]*100] 

display.clear(lcd.BLACK) # 清屏
display.show()
i = 0
chart.draw_axis() # 绘制轴
while True:
    display.clear()
    value = audio.loudness()  # 获取声音响度值
    # 数扰移位，以实现数据动态变化
    for j in range(99):
        line_data[0][j] = line_data[0][j+1]
    line_data[0][99] = value 
    # 绘制图表
    chart.draw_line_chart(line_data, show_grid=False)
    i = (i + 1) % len(line_data[0])
    display.show()
    time.sleep_ms(50)
    