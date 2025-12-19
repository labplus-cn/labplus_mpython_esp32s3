from mpython import *
  
chart = MicroChart(x0 = 80, y0 = 50, val_max = 10, data_colors=[lcd.RED, lcd.GREEN, lcd.BLUE, lcd.WHITE])
tft_lcd.clear()
bar_data = [[3, 8, 5, 10], [6, 4, 9, 7], [2, 7, 4, 9]]
chart.draw_bar_chart(bar_data)
tft_lcd.show()