from mpython import *

gui = UI(tft_lcd)

tft_lcd.clear(lcd.BLACK)
gui.ProgressBar(140, 10, 180, 20, 20, lcd.BLUE)
gui.stripBar(140, 40, 180, 20, 50, dir=1, color=lcd.BLUE)

# tft_lcd.clear(lcd.BLACK)
# gui.qr_code("Hello, mPython!", 10, 10)
gui.qr_code("Hello, mPython!", 10, 10, scale=4)
tft_lcd.show()