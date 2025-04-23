# from mpython import *

# '''
# 1、按键功能测试
# 说明：按A键，终端打印A，B键，打印B
# '''

# button A
from mpython import *

def on_button_a_pressed(_):
    print('A')

button_a.event_pressed = on_button_a_pressed

# button B
def on_button_b_pressed(_):
    print('B')

button_b.event_pressed = on_button_b_pressed

# ----------------------------------------------
# 2、温湿度
from mpython import *
import time

while True:
    print(sht20.temperature())
    time.sleep(1)
    
# ----------------------------------------------
# 3、数字光线
from mpython import *
import time

while True:
    print(light.read())
    time.sleep(1)
    
# ----------------------------------------------
# 4、旋钮电位器
from mpython import *

while True:
    print(pot.read())
    time.sleep_ms(300)

# ----------------------------------------------

# 5、红外探测
from mpython import *

while True:
    print(ir1.read())
    time.sleep_ms(300)
    
while True:
    print(ir2.read())
    time.sleep_ms(300)
    
# ----------------------------------------------
# '''
# 6、声音触发器
# 说明：打印声音采样值
# '''
from mpython import *
import time

while True:
    print(sound.read())
    time.sleep_ms(50)
   
# ----------------------------------------------
# 7、RFID  
from mpython import *
import time

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
    
# ----------------------------------------------
# 8、蜂鸣器
from mpython import *
import music
music.pitch(131, 500, Pin.P12)

# ----------------------------------------------
# 9、RGB
from mpython import *

rgb.fill((int(255), int(0), int(0)))
rgb.write()
time.sleep_ms(1)

# ----------------------------------------------
# 10、编码电机
from mpython import *

encoder_motor.move(100,100)

# ----------------------------------------------
# 11、超声波
from mpython import *

import time
while True:
    print(get_distance())
    time.sleep(1)
    
# ----------------------------------------------
# 12、电池电量
from mpython import *

while True:
    print(get_bat_level())
    time.sleep(1)
    
# ----------------------------------------------
# 13、循迹
from mpythonbox import *

l = Line_follow()

import time
while True:
    print(l.get_val())
    time.sleep(1)
    
# ----------------------------------------------
# 14、风扇、水泵
from mpython import *       
set_speed(1, 100)
set_speed(2, 100)   

# ----------------------------------------------
# 15、摄像头
'''
人脸、猫脸检测
'''
import AIcamera

isDetect = False

def cb(_):
    global isDetect
    isDetect = True

'''
可选参数：
    AIcamera.FACE_DETECTION      # 人脸检测
    AIcamera.FACE_RECOGNITION    # 人脸识别
    AIcamera.CAT_FACE_DETECTION  # 猫脸检测
    AIcamera.COLOR_DETECTION     # 颜色识别
    AIcamera.MOTION_DEECTION     # 运动检测
    AIcamera.CODE_SCANNER        # 二维码识别
'''
AIcamera.init(AIcamera.FACE_DETECTION,cb)

while True:
    if isDetect:
        print(AIcamera.get_result())  #获取识别结果，检测到的人脸框选位置及关键点坐标（口左角，口右角，鼻，左眼，右眼）,数据结构：（{'box': (x1, y1, x2, y2）, 'keypoint': (a0, ... , a9)}, {'box': (x1, y1, x2, y2）, 'keypoint': (a0, ... , a9)}, ...)
        # 用户可以利用识别结果做点别的。
        isDetect = False  # 清除识别标记
    time_sleep_ms(100)


# ----------------------------------------------
# 16、音乐播放
from mpython import *
import audio

my_wifi = wifi()
my_wifi.connectWiFi("office", "wearemaker")

audio.play('http://cdn.makeymonkey.com/test/test.mp3')
time.sleep(10)
audio.stop()

# ----------------------------------------------
# 17、录音
from mpython import *
import audio

audio.record('2.wav', 5, 16, 2, 16000)
time.sleep(5)
audio.play('2.wav')