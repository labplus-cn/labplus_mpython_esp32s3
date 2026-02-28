# main.py -- put your code here!from mpython import *
from mpython import *
import xiaozhi

def show_code(code, msg):
    print("Activate at: https://xiaozhi.me  Code:", code)
    display.clear(lcd.GREEN) # 清屏，背景设置为绿色
    display.DispChar("请在小智官网添加设备，输入以下验证码", 0, 20, lcd.WHITE, True)
    display.DispChar(code, 100, 110, lcd.WHITE, False)
    display.show()
    
my_wifi = wifi()
# my_wifi.connectWiFi("labplus_dev", "helloworld")
my_wifi.connectWiFi("office", "wearemaker")

xiaozhi.activate("https://api.tenclass.net/xiaozhi/ota/", on_code=show_code)