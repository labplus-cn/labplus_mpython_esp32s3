import time
import gc
import uos
from flashbdev import bdev
from neopixel import NeoPixel
import machine 
import ubinascii

# 上电后立即关闭rgb,防止随机灯亮问题
_rgb = NeoPixel(machine.Pin(33, machine.Pin.OUT), 4, 3, 1,0.1)
_rgb.write()
del _rgb

# 硬件复位标志
for count in range(3):
    print("=$%#=")
    time.sleep_ms(50)


# mac地址
mac = '$#mac:{}#$'.format(ubinascii.hexlify(machine.unique_id()).decode().upper())
print(mac)


try:
    if bdev:
        uos.mount(bdev, "/")
except OSError:
    import inisetup
    vfs = inisetup.setup()

gc.collect()
