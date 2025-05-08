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
from mpython import i2c, MPythonPin, PinMode, numberMap
from micropython import const
from machine import UART, Pin
import ustruct
import time

from ATGM336H_5N import GPS
from max30102 import MAX30102

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
        time.sleep_ms(100)
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
            time.sleep_ms(120)
            t = self.i2c.readfrom(0x23, 2)
            time.sleep_ms(10)
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
            time.sleep_ms(2)
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
        time.sleep_ms(100)

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
        time.sleep_ms(50)
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
                return ustruct.unpack("B", temp)[0]
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
            data = ustruct.unpack(">h", temp)
            time.sleep_ms(20)
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


'''
编码电机
'''

MOTOR_right = const(0x01)
"""
M1电机编号，0x01
"""

MOTOR_left = const(0x02)
"""
M2电机编号，0x02
"""
i2c_scan = i2c.scan()

_speed_buf = {}

class EncoderMotor(object):
    def __init__(self):
        self.batch = -1 
        if 18 in i2c_scan:
            # 编码电机(新)
            self.i2c_addr = 18
            self.batch = 1 
            self.stop()
            # print('编码电机')
        else:
            print('编码电机有故障！')
        
    def stop(self):
        if (self.batch == 1):
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
        if (self.batch == 1):
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
        elif (self.batch == 0):
            print('编码电机才支持')
            pass

    def move_distance(self, speed, distance):
        """
        设置小车移动动指定距离，单位:mm 可前进后退
        :param int speed: 电机速度 -100 -- 100。
        :param int distance: 移动距离 0 --- 65535 mm
        """
        distance = distance*10
        if (self.batch == 1):
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
        elif (self.batch == 0):
            print('编码电机才支持')
            return

    def set_correct(self, correct):
        """
        设置小车移动指定距离可转向时修正系数，以修正精确度
        :param int correct: 修正系数 -100 -- 100
        """
        if (self.batch == 1):
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
        elif (self.batch == 0):
            print('编码电机才支持')
            return
        
    def motor_run(self,num,speed):
        """"
        单个编码电机驱动num:电机编号 M1:1 M2:2 speed:转速量程 -100-100
        """
        i2c.writeto(self.i2c_addr,bytearray([8, num, speed]))
    
    def setvater_pump(self,speed):
        """
        水泵开关 speed:转速量程:-100-180
        """
        i2c.writeto(self.i2c_addr,bytearray([7, speed]))

    def motor_info(self,num,off):
        """ 串口获取速度值输出 num:电机编号 M1:1 M2:2 off:开关 1:开  0:关"""
        i2c.writeto(self.i2c_addr,bytearray([9, num, off]))
        
'''
循迹传感器
'''
class LineFollow(object):
    def __init__(self,num1,num2):
        if(isinstance(num1, int) and isinstance(num2, int)):
            self.pin1 = MPythonPin(num1, PinMode.ANALOG)
            self.pin2 = MPythonPin(num2, PinMode.ANALOG)
            self.threshold = [2000, 2000]
        else:
            raise ValueError('参数错误')

    def detect(self,num):
        '''是否探测到，num 1/2 return int 0/1'''
        if(isinstance(num, int)):
            tmp = self.get_raw_val()[num-1]
            if(tmp<=self.threshold[num-1]):
                return 1
            else:
                return 0

    def get_raw_val(self):
        ''' list [0,0]  0-4095 '''
        try:
            tmp = [max(min(self.pin1.read_analog(), 4095), 0),max(min(self.pin2.read_analog(), 4095), 0)]
            return tmp
        except Exception as e:
            print(e)
            return [-1,-1]
       
    def set_threshold(self, threshold):
        '''设置阈值'''
        if(isinstance(threshold, list)):
            if(len(threshold) == 2):
                self.threshold = threshold
            else:
                raise ValueError('参数错误')
        else:
            raise ValueError('参数错误')
                  
''' 离线语音识别 '''
class ASRPRO(object):
    def __init__(self, tx=Pin.P16, rx=Pin.P15, uart_num=1):
        self.uart = UART(uart_num, baudrate=115200, rx=rx, tx=tx)
        self.identifying_word = -1

    def any(self):
        time.sleep_ms(10)
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


'''
DC01 PM2.5驱动
'''
class PM25_DC(object):
    def __init__(self, tx=Pin.P1, rx=Pin.P0, uart_num=1):
        self.K = 0.4 # (注:户读取到的灰尘传感器原始 PM2.5，需要参照 TSI仪器光度法标定一个K 值系数，一般建议 0.4)
        self.uart = UART(uart_num, baudrate=9600, stop=1, tx=tx, rx=rx, timeout=30)
        time.sleep_ms(100)

    def read(self): #单位 微克/立方米
        _pm25 = -1 
        data = bytes(0x00)
        time_cnt = time.ticks_ms()
        while True:
            time.sleep_ms(5)
            if self.uart.any():
                head = self.uart.read(1)   
                if(head[0] == 0xA5):
                    data = head
                    res = self.uart.read(3)
                    data = head + res 
                else:
                    # print('0000')
                    pass
                
                if len(data)==4:
                    DATAH = data[1]
                    DATAL = data[2]
                    sum = 0xA5 + DATAH + DATAL # 计算校验和
                    sum = sum ^ 0x80 # ^异或，得到低7位数据
                    if(sum == data[3]):
                        # _pm25 = (data[1]*128) + data[2]
                        _pm25 = self.K * ((DATAH << 7) | (DATAL & 0x7F))   # 校验成功，计算浓度值
                        break
                    else:
                        pass
            elif time.ticks_ms() - time_cnt > 2000:
                break
        return _pm25
    