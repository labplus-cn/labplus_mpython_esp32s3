# # 1、超声波
# from hcsr04 import HCSR04
# from mpython import *
# import time

# hcsr04 = HCSR04(trigger_pin=Pin.P25, echo_pin=Pin.P24)
# while True:
#     print(hcsr04.distance_mm())
#     time.sleep(1)
    
# # 2、A B按键
# from mpython import *

# def on_button_a_pressed(_):
#     print('A')

# button_a.event_pressed = on_button_a_pressed

# def on_button_b_pressed(_):
#     print('B')

# button_b.event_pressed = on_button_b_pressed

# # 3、温湿度
# from mpython import *

# from bluebit import SHT20

# import time

# sht20 = SHT20()
# while True:
#     print(sht20.temperature())
#     time.sleep_ms(100)

# # 4、数字光线
# from mpython import *
# import time

# while True:
#     print(light.read())
#     time.sleep_ms(100)
 
# # 5、声音触发   
# from mpython import *
# import time

# while True:
#     print(sound.read())
#     time.sleep_ms(100)

# #6、RFID  
# from mpython import *

# import time

# from bluebit import *

# def on_button_a_down(_):
#     global a, b, c, d, DIR
#     time.sleep_ms(10)
#     if button_a.value() == 1: return
#     rf = scan_rfid.scanning()
#     if rf:
#         print(rf.serial_number())
#         rf.set_purse()
#     else:
#         print('未检测到射频卡')

# def on_button_b_down(_):
#     global a, b, c, d, DIR
#     time.sleep_ms(10)
#     if button_b.value() == 1: return
#     rf = scan_rfid.scanning()
#     if rf:
#         rf.increment(10)
#         print(rf.get_balance())
#     else:
#         print('未检测到射频卡')

# scan_rfid = Scan_Rfid()

# button_a.irq(trigger=Pin.IRQ_FALLING, handler=on_button_a_down)

# button_b.irq(trigger=Pin.IRQ_FALLING, handler=on_button_b_down)

# rfid1
# from mpython import *
# import time
# from mfrc import *

# def on_button_a_down(_):
#     global cap, frame, ret
#     time.sleep_ms(10)
#     if button_a.value() == 1: return
#     print(rfid.get_serial_num())
#     rfid.set_purse()

# def on_button_b_down(_):
#     global cap, frame, ret
#     time.sleep_ms(10)
#     if button_b.value() == 1: return
    
#     rfid.increment(10)
#     print(rfid.get_balance())

# rfid = Rfid(i2c = i2c, i2c_addr = 43)

# button_a.irq(trigger=Pin.IRQ_FALLING, handler=on_button_a_down)

# button_b.irq(trigger=Pin.IRQ_FALLING, handler=on_button_b_down)

# #7、蜂鸣器
# from mpython import *
# import music
# music.pitch(131, 500, Pin.P12)

# # 8、RGB
# from mpython import *

# import neopixel

# my_rgb = neopixel.NeoPixel(Pin(Pin.P7), n=1, bpp=3, timing=1)
# my_rgb.fill( (255, 0, 0) )
# my_rgb.write()

# #9、舵机
# from mpython import *
# from servo import Servo

# servo_0 = Servo(23, min_us=500, max_us=2500, actuation_range=180)
# servo_0.write_angle(60)

# # 10、红外探测
# from mpython import *

# ir1 = IR(2)
# while True:
#     print(ir1.read())
#     time.sleep_ms(100)

# from mpython import *

# IrObstacle_4 = InfraredDetection(4)
# IrObstacle_4.set_threshold(1500)
    
# # 11、电机
# from mpython import ledong_shield
# ledong_shield.set_motor(2, 60)

# # 12、录音
# from mpython import *
# import audio

# audio.record('2.wav', 3, 16, 2, 16000)
# time.sleep(6)
# audio.play('2.wav')



# msa311 三軸
from mpython import *
offset_x = 0 
offset_y = 0
offset_z = 0

accelerometer.auto_calibrate() #自動校準
accelerometer.set_nvs_offset(offset_x,offset_y,offset_z) #手動校準
accelerometer.set_g_range(g_range=G_RANGE_2G) #G_RANGE_2G G_RANGE_4G G_RANGE_8G G_RANGE_16G


def cb1():
    print('敲擊')

def cb2():
    print('方向检测')
    
def cb3():
    print('活動')

accelerometer.configure_tap_detection(callback=cb1)
accelerometer.configure_orientation_detection(callback=cb2)
accelerometer.configure_active_detection(callback=cb3)

while True:
    print(accelerometer.read_accel())
    print(accelerometer.get_x())
    print(accelerometer.get_y())
    print(accelerometer.get_z())
    time.sleep(1)