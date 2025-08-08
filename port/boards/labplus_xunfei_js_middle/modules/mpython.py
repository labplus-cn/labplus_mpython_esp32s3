# labplus mPython library
# MIT license; Copyright (c) 2018 labplus
# V1.0 Zhang KaiHua(apple_eat@126.com)

# mpython buildin periphers drivers

# history:
# V1.1 add oled draw function,add buzz.freq().  by tangliufeng
# V1.2 add servo/ui class,by tangliufeng
# ledong_pro 202411

from machine import I2C, PWM, Pin, ADC, TouchPad
i2c = I2C(0, scl=Pin(43), sda=Pin(44), freq=400000)
import esp, math, time, network
import ustruct, array
from neopixel import NeoPixel

import time
from micropython import schedule,const
from esp32 import NVS
from _ntptime import *
from ltr308 import *

i2c_addr=i2c.scan()

if '_print' not in dir(): _print = print

def print(_t, *args, sep=' ', end='\n'):
    _s = str(_t)[0:1]
    if u'\u4e00' <= _s <= u'\u9fff':
        _print(' ' + str(_t), *args, sep=sep, end=end)
    else:
        _print(_t, *args, sep=sep, end=end)

def numberMap(inputNum, bMin, bMax, cMin, cMax):
    outputNum = 0
    outputNum = ((cMax - cMin) / (bMax - bMin)) * (inputNum - bMin) + cMin
    return outputNum

# my_wifi = wifi()
#多次尝试连接wifi
def try_connect_wifi(_wifi, _ssid, _pass, _times):
    if _times < 1: return False
    try:
        print("Try Connect WiFi ... {} Times".format(_times) )
        _wifi.connectWiFi(_ssid, _pass)
        if _wifi.sta.isconnected(): return True
        else:
            time.sleep(5)
            return try_connect_wifi(_wifi, _ssid, _pass, _times-1)
    except:
        time.sleep(5)
        return try_connect_wifi(_wifi, _ssid, _pass, _times-1)

class PinMode(object):
    IN = 1
    OUT = 2
    PWM = 3
    ANALOG = 4
    OUT_DRAIN = 5
# P3: 阻性器件 P5: A P10: sound P11: B P12: buzzer P7: RGB LED
#                   P0 P1 P2 P3 P4 P5 P6 P7 P8  P9 P10 P11 P12 P13 P14 P15 P16        P19  P20 P21    P23 P24 P25 P26 P27 P28
#                                  *     *          *  *   *                          scl  sda *       P  Y   T   H   O   N
pins_remap_esp32 = (1, 2, 3, 4, 5, 0, 7, 8, 15, -1, 6, 46, 21, -1, -1, 48, -1, -1, -1, 43, 44, -1, -1, 9, -1, -1, -1, -1, -1)

class MPythonPin():
    def __init__(self, pin, mode=PinMode.IN, pull=None):
        if mode not in [PinMode.IN, PinMode.OUT, PinMode.PWM, PinMode.ANALOG, PinMode.OUT_DRAIN]:
            raise TypeError("mode must be 'IN, OUT, PWM, ANALOG,OUT_DRAIN'")
        if pin == 10:
            raise TypeError("P10 is used for internalsound sensor")
        if pin == 4 or pin == 6:
            raise TypeError("P4 or P6 is used for internal IR.")
        if pin == 5 or pin == 11:
            raise TypeError("P5 or P11 is used for internal A B key.")
        if pin == 7:
            raise TypeError("P21 is used for internal RGB LED.")
        # if pin == 23:
        #     raise TypeError("P23 is used for internal servo.")
        if pin == 12:
            raise TypeError("P12 is used for internal buzzer.")
        try:
            self.id = pins_remap_esp32[pin]
        except IndexError:
            raise IndexError("Out of Pin range")
        if mode == PinMode.IN:
            if pin not in [0, 1, 2, 3, 4, 6, 8, 9, 13, 14, 15, 16]:
                raise TypeError('IN not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
        if mode == PinMode.OUT:
            if pin not in [0, 1, 2, 3, 4, 6, 8, 9, 13, 14, 15, 16]:
                raise TypeError('OUT not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
            time.sleep_ms(1)
            self.Pin = Pin(self.id, Pin.OUT, pull)
        if mode == PinMode.OUT_DRAIN:
            if pin not in [0, 1, 2, 3, 4, 6, 8, 9, 13, 14, 15, 16]:
                raise TypeError('OUT_DRAIN not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
            time.sleep_ms(1)
            self.Pin = Pin(self.id, Pin.OPEN_DRAIN, pull)
        if mode == PinMode.PWM:
            if pin not in [0, 1, 2, 3, 4, 6, 8, 9, 13, 14, 15, 16]:
                raise TypeError('PWM not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
            time.sleep_ms(1)
            self.pwm = PWM(Pin(self.id), duty=0)
        if mode == PinMode.ANALOG:
            if pin not in [0, 1, 2, 3, 4, 6, 7]:
                raise TypeError('ANALOG not supported on P%d' % pin)
            self.adc = ADC(Pin(self.id))
            self.adc.atten(ADC.ATTN_11DB)
        self.mode = mode

    def irq(self, handler=None, trigger=Pin.IRQ_RISING):
        if not self.mode == PinMode.IN:
            raise TypeError('the pin is not in IN mode')
        return self.Pin.irq(handler, trigger)

    def read_digital(self):
        if not self.mode == PinMode.IN:
            raise TypeError('the pin is not in IN mode')
        return self.Pin.value()

    def write_digital(self, value):
        if self.mode not in [PinMode.OUT, PinMode.OUT_DRAIN]:
            raise TypeError('the pin is not in OUT or OUT_DRAIN mode')
        self.Pin.value(value)

    def read_analog(self):
        if not self.mode == PinMode.ANALOG:
            raise TypeError('the pin is not in ANALOG mode')
        return self.adc.read()
        

    def write_analog(self, duty, freq=1000):
        if not self.mode == PinMode.PWM:
            raise TypeError('the pin is not in PWM mode')
        self.pwm.freq(freq)
        self.pwm.duty(duty)

class wifi:
    def __init__(self):
        self.sta = network.WLAN(network.STA_IF)
        self.ap = network.WLAN(network.AP_IF)

    def connectWiFi(self, ssid, passwd, timeout=10):
        if self.sta.isconnected():
            self.sta.disconnect()
        self.sta.active(True)
        list = self.sta.scan()
        for i, wifi_info in enumerate(list):
            try:
                if wifi_info[0].decode() == ssid:
                    self.sta.connect(ssid, passwd)
                    wifi_dbm = wifi_info[3]
                    break
            except UnicodeError:
                self.sta.connect(ssid, passwd)
                wifi_dbm = '?'
                break
            if i == len(list) - 1:
                raise OSError("SSID invalid / failed to scan this wifi")
        start = time.time()
        print("Connection WiFi", end="")
        while (self.sta.ifconfig()[0] == '0.0.0.0'):
            if time.ticks_diff(time.time(), start) > timeout:
                print("")
                raise OSError("Timeout!,check your wifi password and keep your network unblocked")
            print(".", end="")
            time.sleep_ms(500)
        print("")
        print('WiFi(%s,%sdBm) Connection Successful, Config:%s' % (ssid, str(wifi_dbm), str(self.sta.ifconfig())))

    def disconnectWiFi(self):
        if self.sta.isconnected():
            self.sta.disconnect()
        self.sta.active(False)
        print('disconnect WiFi...')

    def enable_APWiFi(self, essid, password=b'',channel=10):
        self.ap.active(True)
        if password:
            authmode=4
        else:
            authmode=0
        self.ap.config(essid=essid,password=password,authmode=authmode, channel=channel)

    def disable_APWiFi(self):
        self.ap.active(False)
        print('disable AP WiFi...')

# 3 rgb leds
rgb = NeoPixel(Pin(8, Pin.OUT), 1, 3, 1, brightness=0.3)
rgb.write()

# msa311 三軸
if 98 in i2c_addr:
    from msa311 import *
    accelerometer = MSA311(i2c=i2c,g_range=G_RANGE_4G) 

# light sensor LTR-308ALS 
if 83 in i2c_addr:    
    light = LTR_308ALS(i2c)

# sound sensor
sound = ADC(Pin(6))
sound.atten(sound.ATTN_11DB)

# IR 紅外探測
ir1 = ADC(Pin(5))
ir1.atten(ir1.ATTN_11DB)
ir2 = ADC(Pin(7))
ir2.atten(ir2.ATTN_11DB)        

class InfraredDetection:
    def __init__(self, pin):
        self.threshold = 1500
        self._ir = None
        if pin == 4: self._ir = ir1
        elif pin == 6: self._ir = ir2

    def set_threshold(self, val):
        self.threshold = val

    def get_raw_val(self):
        return self._ir.read()

    def detect(self):
        return self._ir.read() <= self.threshold

# buttons
class Button:
    def __init__(self, pin_num, reverse=False):
        self.__reverse = reverse
        (self.__press_level, self.__release_level) = (0, 1) if not self.__reverse else (1, 0)
        self.__pin = Pin(pin_num, Pin.IN, pull=Pin.PULL_UP)
        self.__pin.irq(trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING, handler=self.__irq_handler)
        # self.__user_irq = None
        self.event_pressed = None
        self.event_released = None
        self.__pressed_count = 0
        self.__was_pressed = False
        # print("level: pressed is {}, released is {}." .format(self.__press_level, self.__release_level))
    

    def __irq_handler(self, pin):
        irq_falling = True if pin.value() == self.__press_level else False
        # debounce
        time.sleep_ms(10)
        if self.__pin.value() == (self.__press_level if irq_falling else self.__release_level):
            # new event handler
            # pressed event
            if irq_falling:
                if self.event_pressed is not None:
                    schedule(self.event_pressed, self.__pin)
                # key status
                self.__was_pressed = True
                if (self.__pressed_count < 100):
                    self.__pressed_count = self.__pressed_count + 1
            # release event
            else:
                if self.event_released is not None:
                    schedule(self.event_released, self.__pin)

                
    def is_pressed(self):
        if self.__pin.value() == self.__press_level:
            return True
        else:
            return False

    def was_pressed(self):
        r = self.__was_pressed
        self.__was_pressed = False
        return r

    def get_presses(self):
        r = self.__pressed_count
        self.__pressed_count = 0
        return r

    def value(self):
        return self.__pin.value()

    def irq(self, *args, **kws):
        self.__pin.irq(*args, **kws)

    def status(self):
        val = self.__pin.value()
        if(val==0):
            return 1
        elif(val==1):
            return 0

button_a = Button(0)
button_b = Button(46)

class SHT20(object):
    """
    温湿度模块SHT20控制类

    :param i2c: I2C实例对象,默认i2c=i2c.
    """

    def __init__(self, i2c=i2c):
        self.i2c = i2c

    def temperature(self):
        """
        获取温度

        :return: 温度,单位摄氏度
        """
        time.sleep_ms(10)
        try:
            self.i2c.writeto(0x40, b'\xf3')
            time.sleep_ms(70)
            t = i2c.readfrom(0x40, 2)
            return -46.86 + 175.72 * (t[0] * 256 + t[1]) / 65535
        except Exception as e:
            return -1

    def humidity(self):
        """
        获取湿度

        :return: 湿度,单位%
        """
        time.sleep_ms(10)
        try:
            self.i2c.writeto(0x40, b'\xf5')
            time.sleep_ms(25)
            t = i2c.readfrom(0x40, 2)
            return -6 + 125 * (t[0] * 256 + t[1]) / 65535
        except Exception as e:
            return -1
        
sht20 = SHT20()
# shield 
class Ledong_shield(object):
    def __init__(self):
        self.speed = 0 
        self.i2c = i2c
        self.i2c_addr = 17

    def set_motor(self, motor_num, speed):
        self.speed = speed
        if self.speed > 100:
            self.speed = 100
        if self.speed < -100:
            self.speed = -100
        self.i2c.writeto(self.i2c_addr, bytearray([motor_num, self.speed]), True)

    def power_off(self):
        self.i2c.writeto(self.i2c_addr, b'\x06\x01', True)

    def get_battery_level(self):
        self.i2c.writeto(self.i2c_addr, b'\x03', True)
        time.sleep_ms(2)
        tmp = self.i2c.readfrom(self.i2c_addr, 2)
        data = tmp[1] << 8 +  tmp[0]
        data = max(min(data, 4200), 3300)
        return data


ledong_shield = Ledong_shield()


"""
uuid
"""
def uuid():
    import ubinascii,machine
    return ubinascii.hexlify(machine.unique_id()).decode().upper()