# The MIT License (MIT)

# Copyright (c) 2018, Tangliufeng. LabPlus

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
"""
| blue:bit modules library for mPython. 
| more about with bluebit info browse http://wiki.labplus.cn/index.php?title=Bluebit
"""
from mpython import i2c, sleep_ms, MPythonPin, PinMode, numberMap
from micropython import const
from machine import UART, ADC, Pin
import framebuf
import ubinascii
import struct
import math
from max30102 import MAX30102
from CSK6011A import SpeechSynthesis
from acd1200 import ACD1200

class Color(object):
    """
    颜色模块控制类

    :param i2c: I2C实例对象,默认i2c=i2c.
    """
    def __init__(self, i2c=i2c):
        self.i2c = i2c

    def getRGB(self):
        """
        获取RGB值

        :return: 返回RGB三元组,(r,g,b)
        """
        color = [0, 0, 0]
        self.i2c.writeto(0x0a, bytearray([1]))
        sleep_ms(100)
        self.i2c.writeto(0x0a, bytearray([2]))
        state = self.i2c.readfrom(0x0a, 1)
        if state[0] == 3:
            self.i2c.writeto(0x0a, bytearray([3]))
            c = self.i2c.readfrom(0x0a, 6)
            color[0] = c[5] * 256 + c[4]  # color R
            color[1] = c[1] * 256 + c[0]  # color G
            color[2] = c[3] * 256 + c[2]  # color B
            maxColor = max(color[0], color[1], color[2])
            if maxColor > 255:
                scale = 255 / maxColor
                color[0] = int(color[0] * scale)
                color[1] = int(color[1] * scale)
                color[2] = int(color[2] * scale)
        return color

    def getHSV(self):
        """
        获取HSV值

        :return: 返回HSV三元组,(h,s,v)
        """
        rgb = self.getRGB()
        r, g, b = rgb[0], rgb[1], rgb[2]
        r, g, b = r / 255.0, g / 255.0, b / 255.0
        mx = max(r, g, b)
        mn = min(r, g, b)
        df = mx - mn
        if mx == mn:
            h = 0
        elif mx == r:
            h = (60 * ((g - b) / df) + 360) % 360
        elif mx == g:
            h = (60 * ((b - r) / df) + 120) % 360
        elif mx == b:
            h = (60 * ((r - g) / df) + 240) % 360
        if mx == 0:
            s = 0
        else:
            s = df / mx
        v = mx
        return round(h, 1), round(s, 1), round(v, 1)

class AmbientLight(object):
    """
    数字光线模块控制类

    :param i2c: I2C实例对象,默认i2c=i2c.
    """
    def __init__(self, i2c=i2c):
        self.i2c = i2c

    def getLight(self):
        """
        获取光线值

        :return: 返回光线值,单位lux
        """
        try:
            self.i2c.writeto(0x23, bytearray([0x10]))
            sleep_ms(120)
            t = self.i2c.readfrom(0x23, 2)
            sleep_ms(10)
            return int((t[0] * 256 + t[1]) / 1.2)
        except Exception as e:
            return -1

class Ultrasonic(object):
    """
    超声波模块控制类
    :param i2c: I2C实例对象,默认i2c=i2c.
    """
    def __init__(self, i2c=i2c):
        self.i2c = i2c

    def distance(self):
        """
        获取超声波测距

        :return: 返回测距,单位cm
        """
        try:
            self.i2c.writeto(0x0b, bytearray([1]))
            sleep_ms(2)
            temp = self.i2c.readfrom(0x0b, 2)
            distanceCM = (temp[0] + temp[1] * 256) / 10
            distanceCM = max(min(distanceCM, 200), 0)
            return distanceCM
        except Exception as e:
            return -1

class MP3_(object):
    """
    MP3模块
    WT2003H4-16S
    2022.02.12
    """
    def __init__(self, tx=-1, rx=0, uart_num=1):
        self.uart = UART(uart_num, 9600, stop=2, tx=tx, rx=rx)
        self._vol = 15
        self.is_paused = False
        self.set_output_mode(1)
        self.volume(15)

    def _cmdWrite(self, cmd):
        sum = 0
        len = 0
        for i in cmd:
            sum += i
            len += 1

        len += 2
        sum += len
        sum = sum & 0xff

        pakage = [0x7E, len]
        pakage += cmd
        pakage += ([sum, 0xEF])
        self.uart.write(bytearray(pakage))
        # print(len)
        # print(pakage)
        sleep_ms(100)

    def play_song(self, num):
        """
        播放歌曲
        :param int num: 歌曲编号,类型为数字
        """
        var = [0xA2, (num >> 8) & 0xff, num & 0xff]
        self._cmdWrite(var)

    def set_output_mode(self, mode):
        """设置音频输出模式：0：speaker 1: DAC"""
        var = [0xB6, mode]
        self._cmdWrite(var)  
    
    def set_play_mode(self, mode):
        """指定播放模式"""
        var = [0xAF, mode]
        self._cmdWrite(var)

    def pause(self):
        """暂停播放"""
        if self.is_paused == False:
            self.is_paused = True
            var = [0xAA]
            self._cmdWrite(var)

    def stop(self):
        """停止播放"""
        var = [0xAB]
        self._cmdWrite(var)

    def play(self):
        """
        播放,用于暂停后的继续播放
        """
        if self.is_paused:
            self.is_paused = False
            var = [0xAA]
            self._cmdWrite(var)

    def playNext(self):
        """播下一首"""
        var = [0xAC]
        self._cmdWrite(var)

    def playPrev(self):
        """播上一首"""
        var = [0xAD]
        self._cmdWrite(var)

    def volume(self, vol):
        """设置音量 0~30"""
        self._vol = vol
        var = [0xAE, vol]
        self._cmdWrite(var)
        sleep_ms(50)
        # while True:
        #     if(self.uart.any()):
        #         buff = self.uart.read(2)
                # print(buff)
                # break
    
    # def song_num(self):
    #     """查询 SD 卡内音乐文件总数"""
    #     var = [0xC5]
    #     self._cmdWrite(var)
    #     while True:
    #         if(self.uart.any()):
    #             buff = self.uart.read(3)
    #             num = (buff[1] << 8) + buff[2]
    #             if(buff[0]==197):
    #                 return num
    #             else:
    #                 return 0

class IRRecv(object):
    """
    红外接收模块

    :param rx: 接收引脚设置
    :param uart_id: 串口号:1、2
    """
    def __init__(self, rx, uart_id=1):
        self.uart = UART(uart_id, baudrate=115200, rx=rx)

    def recv(self):
        """
        接收数据

        :return int: 返回红外值,类型整形
        """
        if self.uart.any():
            temp = self.uart.read()
            if temp is not None:
                return struct.unpack("B", temp)[0]
            else:
                return None

class IRTrans(object):
    """
    红外发射模块

    :param tx: 发送引脚设置
    :param uart_id: 串口号:1、2
    """

    def __init__(self, tx, uart_id=2):
        self.uart = UART(uart_id, baudrate=115200, tx=tx)

    def transmit(self, byte):
        """
        发送数据

        :param byte byte: 发送数据,单字节
        """
        self.uart.write(byte)

class DelveBit(object):
    """
    实验探究类的blue:bit,适用的模块有电压、电流、磁场、电导率、PH、光电门、气压、力传感器

    :param address: 模块的I2C地址,可再模块拨动选择不同的地址避免冲突。
    :param i2c: I2C实例对象,默认i2c=i2c
    """

    def __init__(self, address, i2c=i2c):
        self.i2c = i2c
        self.address = address

    def common_measure(self):
        """
        获取实验探究类传感器测量值的通用函数

        :return int: 返回传感器测量值,单位:电压(V)、电流(A)、磁场(mT)、电导率(uS/cm)、PH(pH)、光电门(s)、气压(kPa)、力传感器(N)
        """
        try:
            self.i2c.scan()
            temp = self.i2c.readfrom(self.address, 2)
            data = struct.unpack(">h", temp)
            sleep_ms(20)
            return round(data[0] / 100, 2)
        except Exception as e:
            return -1

    def _trigger_receive(self):
        try:
            self.i2c.scan()
            temp = self.i2c.readfrom(self.address, 5, True)
            if temp[0] in [0, 1]:
                return temp
            else:
                temp = b'\xff'
                return temp
        except Exception as e:
            return -1

    def photo_gate(self):
        """
        Photogate Timer 是用来记录刚触发时刻和触发结束时刻的时间。计算信号的正脉宽时间,当输入信号由低变高为触发开始点,由高变低位触发触发结束点,计算之间的时间差。

        :return int: 返回正脉宽时间,单位秒
        """
        old_start = b'\xff'
        while True:
            start = self._trigger_receive()
            if start[0] == 1 and old_start[0] == 255:
                trigger_begin = start[1] * 16777216 + start[2] * 65536 + start[
                    3] * 256 + start[4]
                old_end = start
                while True:
                    end = self._trigger_receive()
                    if old_end[0] == 0 and end[0] == 255:
                        trigger_end = old_end[1] * 16777216 + old_end[
                            2] * 65536 + old_end[3] * 256 + old_end[4]
                        trigger_time = (trigger_end - trigger_begin) / 2041667
                        # print("time:", trigger_time, trigger_begin,
                        #       trigger_end)
                        return trigger_time
                    else:
                        old_end = end
                        continue
            else:
                old_start = start
                continue

class IRObstacle():
    '''
    乐动模块 红外感应传感器
    '''
    def __init__(self, pin):
        '''初始化参数，引脚'''
        self.pin = MPythonPin(pin, PinMode.ANALOG)
        self.threshold = 1500 #默认阈值

    def detect(self):
        '''是否探测到，布尔类型True/False'''
        tmp = self.pin.read_analog()
        if(tmp<=self.threshold):
            return True
        else:
            return False

    def get_raw_val(self):
        '''获取红外探测传感器裸数据，模拟值'''
        return self.pin.read_analog()

    def set_threshold(self, threshold):
        '''设置红外探测传感器阈值，模拟值'''
        self.threshold = threshold

class SoilHumiditySensor():
    '''乐动模块 土壤湿度'''
    def __init__(self, pin):
        self.pin = MPythonPin(pin, PinMode.ANALOG)
        self.threshold = 2500 

    def detect(self):
        '''是否探测到，布尔类型True/False'''
        tmp = self.get_raw_val()
        if(tmp<=self.threshold):
            return True
        else:
            return False

    def get_raw_val(self):
        '''获取土壤湿度传感器裸数据，模拟值:1600-2600 映射 4095-0 '''
        _soil_humidity =  self.pin.read_analog()
        if _soil_humidity > 2600:
            _soil_humidity = 2600
            return int(numberMap(_soil_humidity,1600,2600,4095,0))
        elif _soil_humidity < 1600:
            _soil_humidity = 1600
            return int(numberMap(_soil_humidity,1600,2600,4095,0))
        else:
            return int(numberMap(_soil_humidity,1600,2600,4095,0))

    def set_threshold(self, threshold):
        '''设置土壤湿度传感器阈值，模拟值'''
        self.threshold = threshold

    def read(self):
        _soil_humidity =  self.pin.read_analog()
        return int(numberMap(_soil_humidity,0,4095,4095,0))

class FanPWM():
    def __init__(self, pin):
        self.pin = MPythonPin(pin, PinMode.PWM)
        self.pwm(0)

    def pwm(self,num):
        self.pin.write_analog(int(numberMap(num,0,100,0,1023)))
                       
''' 离线语音识别 '''
class ASRPRO(object):
    def __init__(self, tx=Pin.P16, rx=Pin.P15, uart_num=1):
        self.uart = UART(uart_num, baudrate=115200, rx=rx, tx=tx)
        self.identifying_word = -1

    def any(self):
        sleep_ms(10)
        if(self.uart.any()):
            self.recognition()
            return True
        else:
            return False
    
    def recognition(self):
        try:
            self.identifying_word = int(self.uart.read().decode('UTF-8','ignore'))
        except Exception as e:
            self.identifying_word = -1
            pass
        
