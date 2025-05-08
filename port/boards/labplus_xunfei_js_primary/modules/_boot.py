import time
import gc
import uos
from flashbdev import bdev
from neopixel import NeoPixel
import ubinascii
from machine import I2C, Pin, unique_id

i2c = I2C(0, scl=Pin(43), sda=Pin(44), freq=400000)

# 硬件复位标志
for count in range(3):
    print("=$%#=")
    time.sleep_ms(150)
    
# mac地址
mac = '$#mac:{}#$'.format(ubinascii.hexlify(unique_id()).decode().upper())
print(mac)
    
import lcd
lcd.draw_logo()

# 上电后立即关闭rgb,防止随机灯亮问题
_rgb = NeoPixel(Pin(45, Pin.OUT), 4, 3, 1,0.1)
_rgb.write()
del _rgb

try:
    if bdev:
        uos.mount(bdev, "/")
except OSError:
    import inisetup
    vfs = inisetup.setup()



gc.collect()
