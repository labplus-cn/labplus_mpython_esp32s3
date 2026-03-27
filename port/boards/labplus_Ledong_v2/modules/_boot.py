import time
import gc
import uos
import os
import ubinascii
import machine
from machine import Pin
from flashbdev import bdev
from neopixel import NeoPixel

# 
Pin(21, Pin.OUT, value=0)

print("boot...")

try:
    if bdev:
        uos.mount(bdev, "/")
except OSError:
    import inisetup
    vfs = inisetup.setup()

# 硬件复位标志
for count in range(3):
    print("=$%#=")
    time.sleep_ms(50)

# 板子型号判断  
try:
    print(os.uname()[-1].split(" ")[0])
except:
    print("Unknown machine")
    

# mac地址
try:
    mac = '$#mac:{}#$'.format(ubinascii.hexlify(machine.unique_id()).decode().upper())
    print(mac)
except:
    print('$#mac:{}#$'.format('Unknown mac'))

# import lcd
# lcd.draw_logo()

# 上电后立即关闭rgb,防止随机灯亮问题
_rgb = NeoPixel(Pin(8, Pin.OUT), 4, 3, 1,0.1)
_rgb.write()
del _rgb

gc.collect()
