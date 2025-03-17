import time
import gc
import uos
from flashbdev import bdev
from neopixel import NeoPixel
import machine 
import ubinascii
import lcd

lcd.draw_logo()

# 上电后立即关闭rgb,防止随机灯亮问题
_rgb = NeoPixel(machine.Pin(8, machine.Pin.OUT), 4, 3, 1,0.1)
_rgb.write()
del _rgb

try:
    if bdev:
        uos.mount(bdev, "/")
except OSError:
    import inisetup
    vfs = inisetup.setup()

# 硬件复位标志
for count in range(3):
    print("=$%#=")
    time.sleep_ms(150)


# mac地址
# mac = '$#mac:{}#$'.format(ubinascii.hexlify(machine.unique_id()).decode().upper())
# print(mac)

gc.collect()
