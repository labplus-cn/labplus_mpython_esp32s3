from mpython import *

chart = MicroChart()
line_data = [[0]*100]
# chart.draw_line_chart(line_data, show_grid=False)

# import random
import audio
display.clear(lcd.BLACK)
display.show()
i = 0
chart.draw_axis()
while True:
    display.clear()
    value = audio.loudness() # random.randint(0, 100)
    for j in range(99):
        line_data[0][j] = line_data[0][j+1]
    line_data[0][99] = value # random.randint(0, 50)
    chart.draw_line_chart(line_data, show_grid=False)
    i = (i + 1) % len(line_data[0])
    display.show()
    time.sleep_ms(50)
    
bar_data = [[3, 8, 5, 10], [6, 4, 9, 7], [2, 7, 4, 9]]
chart.draw_bar_chart(bar_data)
display.show()