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

import audio
# audio.init()   # 初始化音频硬件

import xiaozhi
xiaozhi.record_to_file("/rec.opus", 5000)   # 录音 5 秒

import os
print(os.stat("/rec.opus"))   # 确认文件大小
# 文件格式（每帧）：[uint16_be 帧长度][Opus 帧数据]，可用如下 Python 脚本在 PC 上解包帧：


# import struct
# with open("rec.opus", "rb") as f:
#     frames = []
#     while True:
#         hdr = f.read(2)
#         if len(hdr) < 2: break
#         size = struct.unpack(">H", hdr)[0]
#         frames.append(f.read(size))
# print(f"{len(frames)} frames")