from machine import Timer, UART
from mpython import *
import time, ubinascii, framebuf
import machine, music, audio
import ustruct, os
import _thread
import network
import socket
import urequests
from mpython import *
import AIcamera
from mfrc import *
import lcd 
from servo import Servo
import sensor



    
servo_0 = Servo(22, min_us=2500, max_us=500, actuation_range=180)
tracking = Line_follow()
rfid = Rfid(i2c = i2c, i2c_addr = 47)

    

WIFI_SSID = "TP-LINK_C5AB"
WIFI_PASSWORD = ""


wlan = network.WLAN(network.STA_IF)
wlan.active(True)


g_GetDataLock = _thread.allocate_lock()
g_wifi_comm_state = 0
g_wifi_comm_dBm = 0
btn_a = 0
btn_b = 0


p0 = MPythonPin(0, PinMode.OUT)  # OK
p1 = MPythonPin(1, PinMode.OUT)  # OK
p2 = MPythonPin(2, PinMode.OUT)  # OK
p3 = MPythonPin(3, PinMode.OUT)  # OK


g_Color_Index = 0
FREQ = 2000
FREQ_RNAG = 0.2
PEAK = 6000


def connect_wifi():
    global wlan
    if not wlan.isconnected():
        print("Connecting to WiFi...")
        wlan.connect(WIFI_SSID, WIFI_PASSWORD)
        for _ in range(5):
            if wlan.isconnected():
                break
            time.sleep(1)
        if wlan.isconnected():
            print("WiFi Connected!")
            print("IP:", wlan.ifconfig()[0])
            return True
        else:
            print("Connection Failed")
            return False
    return True


def WifiCommTest():
    global g_wifi_comm_state
    while True:
        if connect_wifi():
            g_wifi_comm_state = 1
            return

   


def record():
    print("record_1")
    audio.play('GuangboTicao.mp3')
    print("record_2")
    time.sleep(5)
    

def play_wave(freq):
    global P8
    P8 = MPythonPin(8, PinMode.PWM)
    P8.write_analog(512, freq)


def stop_wave():
    global P8
    P8.pwm.deinit()


# MAC id
machine_id = ubinascii.hexlify(machine.unique_id()).decode().upper()


def on_button_a_pressed(_):
    music.pitch(400, 100)
    global btn_a
    btn_a = 1


# button B
def on_button_b_pressed(_):
    music.pitch(400, 100)
    global btn_b
    btn_b = 1



button_a.event_pressed = on_button_a_pressed
button_b.event_pressed = on_button_b_pressed

# 创建定时器1
tim1 = Timer(1)
tim2 = Timer(2)
g_Color_Index = 0
g_GPIO_State = 0
g_Servo_Index = 0
g_Servo_Direction = 1  # 初始方向：递增


    
def Rgb_Neopixel():
    # 板载RGB测试
    global g_Color_Index
    global g_GPIO_State
    
    color = ((200, 0, 0), (0, 200, 0), (0, 0, 200))
    rgb.fill(color[g_Color_Index]) 
    rgb.write()
    g_Color_Index = g_Color_Index + 1
    g_Color_Index = g_Color_Index % 3

    if g_GPIO_State == 0:
        g_GPIO_State = 1
    else:
        g_GPIO_State = 0
    p0.write_digital(g_GPIO_State)
    p1.write_digital(g_GPIO_State)
    p2.write_digital(g_GPIO_State)
    p3.write_digital(g_GPIO_State)




def Servo_Neopixel():
    
    global g_Servo_Index
    global g_Servo_Direction  # 1 表示递增，-1 表示递减
    
    g_Servo_Index += g_Servo_Direction
    
    # 到达边界时改变方向
    if g_Servo_Index >= 180:
        g_Servo_Index = 180
        g_Servo_Direction = -1  # 改为递减
        time.sleep(1)
    elif g_Servo_Index <= 0:
        g_Servo_Index = 0
        g_Servo_Direction = 1   # 改为递增
        time.sleep(1)

    servo_0.write_angle(g_Servo_Index)
        

def get_signal_strength():
    if wlan.isconnected():
        return wlan.status("rssi")
    return 0
    

lcd.draw_color(lcd.RED)
time.sleep(2)
lcd.draw_color(lcd.GREEN)
time.sleep(1)
lcd.draw_color(lcd.BLUE)
time.sleep(1)
lcd.draw_color(lcd.WHITE)  # 添加白色
time.sleep(1)
lcd.draw_color(lcd.BLACK)  # 添加黑色
time.sleep(1)

sensor.reset()

    
_thread.start_new_thread(WifiCommTest, ())
tim1.init(period=1000, mode=Timer.PERIODIC, callback=lambda t: Rgb_Neopixel())
tim2.init(period=10, mode=Timer.PERIODIC, callback=lambda t: Servo_Neopixel())

def check_list_length(lst, expected_length):
    print(lst)
    return len(lst) == expected_length

while True:
    g_GetDataLock.acquire()
    
    sensor.snapshot()
    sensor.free_fb()
    
    if btn_a and btn_b:
        time.sleep(0.5)
        for freq in range(0, 3, 1):
            music.pitch(400, 90)
            time.sleep(0.1)
        time.sleep(0.5)
        record()
        encoder_motor.turn_angle(3,50,360)
        time.sleep(7)
        encoder_motor.turn_angle(4,50,360)
        time.sleep(7)
        encoder_motor.move_distance(50,20)
        time.sleep(3)
        encoder_motor.move(0,0)
        btn_a = 0
        btn_b = 0
        
    print("----------------------------")
    
    if g_wifi_comm_state:
        g_wifi_comm_dBm = get_signal_strength()
        
    light_value = light.read()
    sound_value = sound.read()
    ultrasound_value = get_distance()
    ir1_value = ir1.read()
    ir2_value = ir2.read()
    pot_value = pot.read()
    rfid_value = str(rfid.get_serial_num())
    temperature_value = int(sht20.temperature())
    humiture_value = int(sht20.humidity())

    tracking_value = tracking.get_val()
    SdaScl_value = int(check_list_length(i2c.scan(),8))
    
    
    
    # 光线
    print('light:%d' % light_value)                     
    
    # 声音
    print('Sound:%d' % sound_value)                     
    
    # 超声波
    print('Ultrasound:%d' % ultrasound_value)           
    
    # 红外探测
    print('Ir1:%d,Ir2:%d' % (ir1_value,ir2_value))      
    
    # 旋钮电位器
    print('Pot:%d' % pot_value)                         
    
    # RFID
    print('Rfid:%s' % rfid_value)                       
    
    # 温度
    print('Humiture:%d' % temperature_value)  
    
    # 湿度
    print('Temperature:%d' % humiture_value)   
    
    # 循迹
    print('Tracking:%d,%d,%d,%d,%d' % (tracking_value[0],tracking_value[1],tracking_value[2],tracking_value[3],tracking_value[4]))      
    
    # WIFI
    print('Wifi:%d,Name:%s,Pass:%s' % (g_wifi_comm_dBm, WIFI_SSID, WIFI_PASSWORD))  
    
    # MAC地址
    print('Mac:{}'.format(machine_id.upper()))          

    # SDA/SCL
    print('SdaScl:%d' % SdaScl_value)            

    
    # 风扇
    set_speed(1, 10)
    
    #水泵
    set_speed(2, 200)  
    
    
    g_GetDataLock.release()



