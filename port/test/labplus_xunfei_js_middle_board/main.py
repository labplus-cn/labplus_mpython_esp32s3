# 1、A B按键
from mpython import *

def on_button_a_pressed(_):
    print('A')

button_a.event_pressed = on_button_a_pressed

def on_button_b_pressed(_):
    print('B')

button_b.event_pressed = on_button_b_pressed

# -----------------------------------------------
# 2、温湿度
from mpython import *

from bluebit import SHT20

import time


while True:
    print(sht20.temperature())
    time.sleep(1)

# -----------------------------------------------
# 3、数字光线
from mpython import *
import time

while True:
    print(light.read())
    time.sleep(1)
 
# -----------------------------------------------
# 4、超声波
from hcsr04 import HCSR04
from mpython import *
import time

hcsr04 = HCSR04(trigger_pin=Pin.P25, echo_pin=Pin.P24)
while True:
    print(hcsr04.distance_mm())
    time.sleep(1)
  
# -----------------------------------------------
# 5、红外探测
from mpython import *

while True:
    print(ir1.read())
    time.sleep_ms(300)
    
while True:
    print(ir2.read())
    time.sleep_ms(300)
    
# -----------------------------------------------
# 6、声音触发   
from mpython import *
import time

while True:
    print(sound.read())
    time.sleep_ms(100)

# -----------------------------------------------
# 7、RFID  1
from mpython import *

import time

from bluebit import *

def on_button_a_down(_):
    global a, b, c, d, DIR
    time.sleep_ms(10)
    if button_a.value() == 1: return
    rf = scan_rfid.scanning()
    if rf:
        print(rf.serial_number())
        rf.set_purse()
    else:
        print('未检测到射频卡')

def on_button_b_down(_):
    global a, b, c, d, DIR
    time.sleep_ms(10)
    if button_b.value() == 1: return
    rf = scan_rfid.scanning()
    if rf:
        rf.increment(10)
        print(rf.get_balance())
    else:
        print('未检测到射频卡')

scan_rfid = Scan_Rfid()

button_a.irq(trigger=Pin.IRQ_FALLING, handler=on_button_a_down)

button_b.irq(trigger=Pin.IRQ_FALLING, handler=on_button_b_down)

# -----------------------------------------------
# 8、rfid 2


# -----------------------------------------------
# 9、加速度计


# -----------------------------------------------
# 10、蜂鸣器
from mpython import *
import music
music.pitch(131, 500, Pin.P12)


# -----------------------------------------------
# 11、RGB
from mpython import *

import neopixel

my_rgb = neopixel.NeoPixel(Pin(Pin.P7), n=1, bpp=3, timing=1)
my_rgb.fill( (255, 0, 0) )
my_rgb.write()

# -----------------------------------------------
# 12、舵机
from mpython import *
from servo import Servo

servo_0 = Servo(23, min_us=500, max_us=2500, actuation_range=180)
servo_0.write_angle(60)

# -----------------------------------------------
# 13、电机
from mpython import ledong_shield
ledong_shield.set_motor(1, 60)
ledong_shield.set_motor(2, 60)

# -----------------------------------------------
# 14、录音、播放
from mpython import *
import audio

audio.record('2.wav', 3, 16, 2, 16000)
time.sleep(6)
audio.play('2.wav')