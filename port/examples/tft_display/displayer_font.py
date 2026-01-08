'''
示例1 基本文字示例
'''
from mpython import *

display.clear(lcd.GREEN) # 清屏，背景设置为绿色
# # 英文、葡萄牙语、日文、中文显示”你好，世界！“
# display.DispChar("hello,world!", 0, 0, lcd.WHITE, False)
# display.DispChar("Olá, mundo!!", 0, 35, lcd.WHITE, False)
# display.DispChar("こんにちは、世界!", 0, 70, lcd.WHITE, False)
display.DispChar("你好，世界！", 0, 105, lcd.WHITE, False)
display.DispChar("圳、阈、泗、绦、门、簧、℃", 0, 34, lcd.WHITE, True)
display.show()

# 刷屏测试
# while True:
#     t1 = time.ticks_ms()
#     display.clear()
#     display.DispChar("中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!", 0, 0, lcd.WHITE, True)
#     # display.DispChar_font(dvsm_21, "hello,world", 50, 105, lcd.RED)
#     display.show()
#     print(time.ticks_diff(time.ticks_ms(), t1))

'''
示例2 设置文本颜色
'''
# from mpython import *

# display.clear(lcd.BLACK) # 清屏，背景设置为黑色
# # 显示不同颜色文字
# display.DispChar("你好，世界！", 0, 0, lcd.RED, False)
# display.DispChar("你好，世界！", 0, 35, lcd.GREEN, False)
# display.DispChar("你好，世界！", 0, 70, lcd.BLUE, False)
# display.DispChar("你好，世界！", 0, 105, lcd.WHITE, False)
# display.show()

'''
示例3 数码管及dvsm文字示例

只能显示拉丁字母和数字。
内置了以下字体：
digiface_16 digiface_21 digiface_30 digiface_44 digiface_it_30 digiface_it_42
dvsm_16 dvsm_21 dvsmb_16 dvsmb_21
'''
# from mpython import *
# import font.dvsm_21 as dvsm_21
# import font.digiface_44 as digiface_44

# display.clear(lcd.BLACK)
# # font文件夹下内置的数码管及dvssm字体显示测试,字体由font-to-py工具生成
# display.DispChar_font(dvsm_21, "hello,world", 50, 40, lcd.RED)
# display.DispChar_font(digiface_44, "12:34", 50, 70, lcd.BLUE)
# display.show()