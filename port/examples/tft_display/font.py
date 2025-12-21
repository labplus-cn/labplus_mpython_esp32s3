from mpython import *
import lcd
import font.dvsm_21 as dvsm_21
import font.digiface_44 as digiface_44

tft_lcd.clear()
# 文字显示测试(思源黑体24号)
tft_lcd.DispChar("中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!", 0, -7, lcd.WHITE, True)

# font文件夹下内置的数码管及dvssm字体显示测试,字体由font-to-py工具生成
tft_lcd.DispChar_font(dvsm_21, "hello,world", 50, 105, lcd.RED)
tft_lcd.DispChar_font(digiface_44, "12:34", 50, 130, lcd.BLUE)
tft_lcd.show()

# 刷屏测试
while True:
    t1 = time.ticks_ms()
    tft_lcd.clear()
    tft_lcd.DispChar("中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!", 0, 0, lcd.WHITE, True)
    # tft_lcd.DispChar_font(dvsm_21, "hello,world", 50, 105, lcd.RED)
    tft_lcd.show()
    print(time.ticks_diff(time.ticks_ms(), t1))