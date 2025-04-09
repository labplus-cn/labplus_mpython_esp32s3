from mpython import *
import time
import struct

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