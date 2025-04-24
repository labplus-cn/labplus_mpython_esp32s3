# labplus mPython library
# MIT license; Copyright (c) 2018 labplus
# V1.0 Zhang KaiHua(apple_eat@126.com)

# mpython buildin periphers drivers

# history:
# V1.1 add oled draw function,add buzz.freq().  by tangliufeng
# V1.2 add servo/ui class,by tangliufeng
# labplus_Ledong_v2 202411

from machine import PWM, Pin, ADC
import time, network
import struct
from neopixel import NeoPixel
from time import sleep_ms
from micropython import schedule,const
from _ntptime import *
from ltr308 import *
from _boot import i2c

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
#                   P0 P1 P2 P3 P4 P5 P6 P7 P8  P9 P10 P11 P12 P13 P14 P15 P16        P19  P20 P21  P22  P23 P24 P25 P26 P27 P28
#                                  *     *          *  *   *                          scl  sda *         P    Y   T   H   O   N
pins_remap_esp32 = (1, 2, 3, 4, 5, 0, 7, 8, -1, -1, 6, 46, 21, -1, -1, 48, 47, -1, -1, 43, 44, 45,  33,  -1, -1, -1, -1, -1, -1)

class MPythonPin():
    def __init__(self, pin, mode=PinMode.IN, pull=None):
        if mode not in [PinMode.IN, PinMode.OUT, PinMode.PWM, PinMode.ANALOG, PinMode.OUT_DRAIN]:
            raise TypeError("mode must be 'IN, OUT, PWM, ANALOG,OUT_DRAIN'")
        if pin == 10:
            raise TypeError("P10 is used for internalsound sensor")
        if pin == 5 or pin == 11:
            raise TypeError("P5 or P11 is used for internal A B key.")
        if pin == 4 or pin == 6:
            raise TypeError("P4 or P6 is used for internal IR.")
        if pin == 12:
            raise TypeError("P12 is used for internal buzzer.")
        if pin == 21:
            raise TypeError("P21 is used for internal RGB led.")
        if pin == 7:
            raise TypeError("P21 is used for internal potentiometer.")
        try:
            self.id = pins_remap_esp32[pin]
        except IndexError:
            raise IndexError("Out of Pin range")
        if mode == PinMode.IN:
            if pin not in [0, 1, 2, 3, 22]:
                raise TypeError('IN not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
        if mode == PinMode.OUT:
            if pin not in [0, 1, 2, 3, 22]:
                raise TypeError('OUT not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
            sleep_ms(1)
            self.Pin = Pin(self.id, Pin.OUT, pull)
        if mode == PinMode.OUT_DRAIN:
            if pin not in [0, 1, 2, 3, 22]:
                raise TypeError('OUT_DRAIN not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
            sleep_ms(1)
            self.Pin = Pin(self.id, Pin.OPEN_DRAIN, pull)
        if mode == PinMode.PWM:
            if pin not in [0, 1, 2, 3, 22]:
                raise TypeError('PWM not supported on P%d' % pin)
            self.Pin = Pin(self.id, Pin.IN, pull)
            sleep_ms(1)
            self.pwm = PWM(Pin(self.id), duty=0)
        if mode == PinMode.ANALOG:
            if pin not in [0, 1, 2, 3]:
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

'''
# to be test
class LightSensor(ADC):
    
    def __init__(self):
        super().__init__(Pin(pins_remap_esp32[4]))
        # super().atten(ADC.ATTN_11DB)
    
    def value(self):
        # lux * k * Rc = N * 3.9/ 4096
        # k = 0.0011mA/Lux
        # lux = N * 3.9/ 4096 / Rc / k
        return super().read() * 1.1 / 4095 / 6.81 / 0.011
    
'''

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
rgb = NeoPixel(Pin(45, Pin.OUT), 4, 3, 1, brightness=0.3)
rgb.write()


# light sensor LTR-308ALS 
light = LTR_308ALS(i2c)

# sound sensor
sound = ADC(Pin(6))
sound.atten(sound.ATTN_11DB)

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
        sleep_ms(10)
        try:
            self.i2c.writeto(0x40, b'\xf3')
            sleep_ms(70)
            t = i2c.readfrom(0x40, 2)
            return -46.86 + 175.72 * (t[0] * 256 + t[1]) / 65535
        except Exception as e:
            return -1

    def humidity(self):
        """
        获取湿度

        :return: 湿度,单位%
        """
        sleep_ms(10)
        try:
            self.i2c.writeto(0x40, b'\xf5')
            sleep_ms(25)
            t = i2c.readfrom(0x40, 2)
            return -6 + 125 * (t[0] * 256 + t[1]) / 65535
        except Exception as e:
            return -1

sht20 = SHT20()

# IR
ir1 = ADC(Pin(5))
ir1.atten(ir1.ATTN_11DB)
ir2 = ADC(Pin(7))
ir2.atten(ir2.ATTN_11DB)

# POT
pot = ADC(Pin(8))
pot.atten(pot.ATTN_11DB)

class Rfid():
    """
    Rfid类,提供读写block和电子钱包操作。

    :param i2c: I2C实例对象
    :param serial_number: RFID卡序列号

    """
    import rfid

    def __init__(self, i2c, serial_number):
        self.i2c = i2c
        self._serial_number = serial_number
        self.purse_block = None

    def _get_serNum(self, serial_number):
        serNumCheck = 0
        buf = serial_number.to_bytes(4, 'little')
        for i in range(4):
            serNumCheck ^= buf[i]
        serNum_list = [int(i) for i in buf]
        serNum_list.append(serNumCheck)
        return (tuple(serNum_list))

    def serial_number(self):
        """
        获取序列号
        """
        return self._serial_number

    def _judge_block(self, block_number):
        """判断block是否可用。

        RFID卡内储存空间分为16 个扇区，每个扇区由4 块（块0、块1、块2、块3）组成，（我们也
        将16 个扇区的64 个块按绝对地址编号为0~63。第0 扇区的块0（即绝对地址0 块），它用于存放厂商代码，已经固化，不可更改。
        每个扇区的块0、块1、块2 为数据块，可用于存贮数据。

        :param block_number: 块编号
        """
        unused_blocked = [i*2 ^ 2-1 for i in range(1, 16)]
        unused_blocked.append(0)
        if block_number in unused_blocked:
            raise Exception(
                "This block {} can't be accessed!" .format(block_number))
        else:
            return True

    def auth(self, block_number=1):
        serNum = self._get_serNum(self._serial_number)
        if self._judge_block(block_number):
            self.rfid.init(self.i2c)
            if self.rfid.find_card():
                self.rfid.anticoll()
                if self.rfid.select_tag(serNum):
                    return self.rfid.auth(serNum, block_number)

    def read_block(self, block_number=1):
        """读取块数据,长度16字节

        :param block_number: 块编号
        """
        self.auth(block_number)
        return self.rfid.read_block(block_number)

    def write_block(self, buf, block_number=1):
        """写块数据,长度16字节

        :param bytes buf: 块编号
        :param int block_number: 块编号
        """
        self.auth(block_number)
        return self.rfid.write_block(block_number, buf)

    def set_purse(self, block_number=1):
        """
        设置电子钱包,默认使用block 1。

        :param int block_number: 块编号
        """
        if block_number != 1:
            self.purse_block = block_number
        else:
            self.purse_block = 1
        self.auth(self.purse_block)
        return self.rfid.set_purse(self.purse_block)

    def get_balance(self):
        """
        获取电子钱包余额。使用该函数前,必须对数据块进行 ``set_purse()`` 设置。

        :return: 返回余额
        """
        if self.purse_block is None:
            self.purse_block = 1
        self.auth(self.purse_block)
        return self.rfid.balance(self.purse_block)

    def increment(self, value):
        """
        给电子钱包充值。使用该函数前,必须对数据块进行 ``set_purse()`` 设置。

        :param int value: 充值 
        """
        if self.purse_block is None:
            self.purse_block = 1
        self.auth(self.purse_block)
        return self.rfid.increment(self.purse_block, value)

    def decrement(self, value):
        """
        给电子钱包扣费。使用该函数前,必须对数据块进行 ``set_purse()`` 设置。

        :param int value: 扣费 
        """
        if self.purse_block is None:
            self.purse_block = 1
        self.auth(self.purse_block)
        return self.rfid.decrement(self.purse_block, value)

class Scan_Rfid():
    """扫描Rfid卡类.
    """
    import rfid

    @classmethod
    def scanning(cls, i2c=i2c):
        """
        扫描RFID卡,返回Rfid对象

        :param obj i2c: I2C实例对象
        :return: 返回Rfid对象
        """
        cls.rfid.init(i2c)
        if cls.rfid.find_card():
            serial_tuple = cls.rfid.anticoll()
            if serial_tuple:
                serial_num = int.from_bytes(bytes(serial_tuple[:-1]), 'little')
                print("find card: {}" .format(serial_num))
                return Rfid(i2c, serial_num)

IIC_ADDR = const(15)
MOTOR_right = const(0x01)
MOTOR_left = const(0x02)

def get_distance():
    i2c.writeto(IIC_ADDR, bytearray([7]))
    return struct.unpack('H', i2c.readfrom(IIC_ADDR, 2))[0]/10

_speed_buf = {}

def set_speed(motor_no, speed): #motor_num 1：风扇 2:水泵
    global _speed_buf
    speed = max(min(speed, 100), -100)
    _speed_buf.update({motor_no: speed})
    attempts = 0
    while True:
        try:
            i2c.writeto(IIC_ADDR, bytearray([1, motor_no, speed]))
        except Exception as e:
            attempts = attempts + 1
            time.sleep_ms(100)
            if attempts > 2:
                break
        else:
            break

def get_speed(motor_no):
    global _speed_buf
    if motor_no in _speed_buf:
        return _speed_buf[motor_no]
    else:
        return None

'''line follow'''
"""
因循迹较耗电，不使用该功能时，关闭其电源。
从MCU中读出的5路循迹值为模拟量， 需要跟给定的阈值比较，大于阈值定义为黑线，值为1
5路循迹值序号：从左到右对应list索引值0-4
"""
class Line_follow(object):
    def __init__(self):
        i2c.writeto(IIC_ADDR, bytearray([3, 1])) # 开循迹电源
        self.power_status = 1 # status: power on
        self.threshold = [2000, 2000, 2000, 2000, 2000]

    def get_val(self):
        i2c.writeto(IIC_ADDR, bytearray([5]))
        tmp = struct.unpack('5H', i2c.readfrom(IIC_ADDR, 10))
        list = [0]*5
        for i in range(5):
            if((tmp[i] > self.threshold[i]) and (tmp[i] < 4096)):
                list[i] = 1
            else:
                list[i] = 0
        return list

    def get_raw_val(self):
        '''获取循迹值裸数据，模拟值'''
        i2c.writeto(IIC_ADDR, bytearray([5]))
        tmp = struct.unpack('5H', i2c.readfrom(IIC_ADDR, 10))
        return tmp

    def set_threshold(self, threshold):
        self.threshold = threshold

    def get_threshold(self):
        return self.threshold

    def on_off(self, on_off):
        if on_off == 1 and self.power_status == 2:
            i2c.writeto(IIC_ADDR, bytearray([3, 1])) # 开
            self.power_status = 1
        elif on_off == 2 and self.power_status == 1:
            i2c.writeto(IIC_ADDR, bytearray([3, 2])) # 关
            self.power_status = 2            


"""
获取电池电量，单位mV
"""
def get_bat_level():
    i2c.writeto(IIC_ADDR, bytearray([4]))
    return struct.unpack('H', i2c.readfrom(IIC_ADDR, 2))

'''
编码电机 2023.3
'''
class EncoderMotor(object):
    def __init__(self): 
        self.i2c_addr = 18
        self.stop()
        print(self.i2c_addr)

    def stop(self):
        attempts=0
        while True:
            try:
                i2c.writeto(self.i2c_addr, bytearray([1]))
            except Exception as e:
                attempts = attempts + 1
                if attempts > 2:
                    break
            else:
                break

    def move(self, speed_l, speed_r):
        """
        设置电机速度
        :param int motor_no: 控制电机编号，可以使用 ``MOTOR_left``, ``MOTOR_right`` ,或者直接写入电机编号。
        :param int speed: 电机速度，范围-100~100，负值代表反转。
        """
        """
        设置小车移动速度，可前进后退
        :param int speed_l: 左电机速度 -100 -- 100。
        :param int speed_r: 右电机速度 -100 -- 100。
        """
        if speed_l < -100:
            speed_l = -100
        if speed_r < -100:
            speed_r = -100
        if speed_l > 100:
            speed_l = 100
        if speed_r > 100:
            speed_r = 100
    
        attempts=0
        while True:
            try:
                i2c.writeto(self.i2c_addr, bytearray([2, speed_l, speed_r]))
            except Exception as e:
                attempts = attempts + 1
                if attempts > 2:
                    break
            else:
                break

    def turn_angle(self, dir, speed, angle):
        """
        设置电机转向 
        :param int dir: 左转： 3 右转： 4
        :param int speed: 左电机速度 0 -- 100。
        :param int angle: 左电机速度 0 -- 360
        """
        if (self.batch == 1):
            if speed < 0:
                speed = 0
            if speed > 100:
                speed = 100
            if dir !=3 and dir != 4:
                return
            tmp = [0]*2
            tmp[0] = angle & 0xff
            tmp[1] = (angle >> 8) & 0xff
         
            attempts=0
            while True:
                try:
                    i2c.writeto(self.i2c_addr, bytearray([dir, speed, tmp[0], tmp[1]]))
                except Exception as e:
                    attempts = attempts + 1
                    if attempts > 2:
                        break
                else:
                    break


    def move_distance(self, speed, distance):
        """
        设置小车移动动指定距离，单位:mm 可前进后退
        :param int speed: 电机速度 -100 -- 100。
        :param int distance: 移动距离 0 --- 65535 mm
        """
        distance = distance*10
        if distance < 0:
            distance = 0
        if distance > 65535:
            distance = 65535
        tmp = [0]*2
        tmp[0] = distance & 0xff
        tmp[1] = (distance >> 8) & 0xff
        attempts=0
        while True:
            try:
                i2c.writeto(self.i2c_addr, bytearray([5, speed, tmp[0], tmp[1]]))
            except Exception as e:
                attempts = attempts + 1
                if attempts > 2:
                    break
            else:
                break

    def set_correct(self, correct):
        """
        设置小车移动指定距离可转向时修正系数，以修正精确度
        :param int correct: 修正系数 -100 -- 100
        """
        if correct < -100:
            correct = -100
        if correct > 100:
            correct = 100
        attempts=0
        while True:
            try:
                i2c.writeto(self.i2c_addr, bytearray([6, correct]))
            except Exception as e:
                attempts = attempts + 1
                if attempts > 2:
                    break
            else:
                break

encoder_motor = EncoderMotor()
# from gui import *
