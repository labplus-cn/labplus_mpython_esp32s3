# gui for mpython
# MIT license; Copyright (c) 2019 Zhang Kaihua(apple_eat@126.com)
import time, math, struct, gc, lcd
import framebuf
import adafruit_miniqr,gc
from mpython import tft_lcd

import math

class MicroChart:
    """
    MicroPython 轻量级图表类，支持折线图、柱状图
    适配 SSD1306 (OLED)、ST7735 (TFT) 等显示屏
    """
    def __init__(self, display = tft_lcd, x0=0, y0=0, width=200, height=80, 
                 bg_color=lcd.BLACK, axis_color=lcd.WHITE, data_colors=[lcd.WHITE, lcd.RED, lcd.BLUE], padding=10):
        """
        初始化图表
        :param display: 显示屏对象（如SSD1306_I2C/ST7735）
        :param x0/y0: 图表左上角坐标
        :param width/height: 图表尺寸
        :param bg_color: 背景色（单色屏：0=黑/1=白；彩色屏：RGB565值）
        :param axis_color: 坐标轴颜色
        :param data_colors: 数据系列颜色列表（循环使用）
        :param padding: 坐标轴边距（像素）
        """
        self.display = display  # 显示屏驱动对象
        self.x0 = x0
        self.y0 = y0
        self.width = width
        self.height = height
        self.bg_color = bg_color
        self.axis_color = axis_color
        self.data_colors = data_colors
        self.padding = padding  # 坐标轴与绘图区的边距

        # 计算有效绘图区域（扣除边距）
        self.plot_x0 = self.x0 + self.padding  # 绘图区左上角X
        self.plot_y0 = self.y0 + 2             # 绘图区左上角Y
        self.plot_width = self.width - self.padding  # 绘图区宽度
        self.plot_height = self.height - self.padding  # 绘图区高度
        self.x_axis_y = self.plot_y0 + self.plot_height  # X轴Y坐标

    def _scale_data(self, data):
        """
        数据缩放：将原始数据映射到绘图区像素范围
        :param data: 二维列表 [[系列1数据], [系列2数据], ...]
        :return: 缩放后的数据、X轴步长、数据极值(min/max)
        """
        # 扁平化数据找极值（忽略None）
        flat_data = [val for series in data for val in series if val is not None]
        if not flat_data:
            return [], 0, 0, 0  # 空数据
        
        min_val = min(flat_data)
        max_val = max(flat_data)
        # 避免极值相同导致除零
        if max_val == min_val:
            max_val += 1
            min_val -= 1

        # 缩放数据：Y值映射到绘图区（屏幕Y轴向下，需反转）
        scaled_data = []
        for series in data:
            scaled_series = []
            for val in series:
                if val is None:
                    scaled_series.append(None)
                    continue
                # 映射公式：val → 绘图区Y坐标
                scaled_y = self.x_axis_y - ((val - min_val) / (max_val - min_val)) * self.plot_height
                scaled_series.append(round(scaled_y))
            scaled_data.append(scaled_series)

        # 计算X轴每个数据点的像素步长
        num_points = max(len(series) for series in data)
        x_step = self.plot_width / (num_points - 1) if num_points > 1 else 1

        return scaled_data, x_step, min_val, max_val

    def draw_background(self):
        """绘制图表背景（填充矩形）"""
        self.display.fill_rect(self.x0, self.y0, self.width, self.height, self.bg_color)

    def draw_axis(self, show_ticks=True):
        """绘制坐标轴（可选刻度）"""
        # 绘制Y轴（垂直）
        y_axis_x = self.plot_x0 - 5
        self.display.line(y_axis_x, self.y0, y_axis_x, self.x_axis_y, self.axis_color)
        # 绘制X轴（水平）
        self.display.line(self.x0, self.x_axis_y, self.x0 + self.width, self.x_axis_y, self.axis_color)

        # 绘制刻度（可选）
        if show_ticks:
            # Y轴刻度（5个）
            tick_count = 5
            tick_step = self.plot_height / (tick_count - 1)
            for i in range(tick_count):
                tick_y = self.x_axis_y - i * tick_step
                self.display.line(y_axis_x - 3, tick_y, y_axis_x, tick_y, self.axis_color)
            # X轴刻度（5个）
            tick_step_x = self.plot_width / (tick_count - 1)
            for i in range(tick_count):
                tick_x = self.plot_x0 + i * tick_step_x
                self.display.line(tick_x, self.x_axis_y, tick_x, self.x_axis_y + 3, self.axis_color)

    def draw_grid(self, grid_color=None):
        """绘制网格线（可选）"""
        if grid_color is None:
            # 单色屏用浅色调，彩色屏用灰度
            grid_color = self.axis_color // 2 if self.axis_color > 1 else 1
        # 水平网格线
        tick_count = 5
        tick_step = self.plot_height / (tick_count - 1)
        for i in range(tick_count):
            y = self.x_axis_y - i * tick_step
            self.display.line(self.plot_x0, y, self.plot_x0 + self.plot_width, y, grid_color)
        # 垂直网格线
        tick_step_x = self.plot_width / (tick_count - 1)
        for i in range(tick_count):
            x = self.plot_x0 + i * tick_step_x
            self.display.line(x, self.plot_y0, x, self.x_axis_y, grid_color)

    def draw_line_chart(self, data, show_grid=False):
        """
        绘制折线图
        :param data: 二维列表 [[系列1数据], [系列2数据], ...]
        :param show_grid: 是否显示网格线
        """
        if not data or all(len(series) == 0 for series in data):
            return  # 空数据不绘制

        # 绘制背景和坐标轴
        self.draw_background()
        self.draw_axis()
        if show_grid:
            self.draw_grid()

        # 缩放数据
        scaled_data, x_step, _, _ = self._scale_data(data)

        # 绘制每个数据系列
        for series_idx, series in enumerate(scaled_data):
            color = self.data_colors[series_idx % len(self.data_colors)]
            prev_x, prev_y = None, None
            for point_idx, y in enumerate(series):
                if y is None:
                    prev_x, prev_y = None, None
                    continue
                # 计算当前点X坐标
                x = self.plot_x0 + point_idx * x_step
                x = round(x)
                # 绘制点
                self.display.pixel(x, y, color)
                # 绘制线段（与前一个点连接）
                if prev_x is not None and prev_y is not None:
                    self.display.line(prev_x, prev_y, x, y, color)
                prev_x, prev_y = x, y

        # 刷新显示屏
        self.display.show()

    def draw_bar_chart(self, data, show_grid=False):
        """
        绘制柱状图
        :param data: 二维列表 [[系列1数据], [系列2数据], ...]
        :param show_grid: 是否显示网格线
        """
        if not data or all(len(series) == 0 for series in data):
            return

        # 绘制背景和坐标轴
        self.draw_background()
        self.draw_axis()
        if show_grid:
            self.draw_grid()

        # 缩放数据
        scaled_data, x_step, _, _ = self._scale_data(data)
        num_series = len(data)
        num_points = max(len(series) for series in data)
        # 计算柱子宽度（多系列错开显示）
        bar_width = (x_step / (num_series + 1)) * 0.8
        bar_width = max(1, bar_width)  # 最小宽度1像素

        # 绘制每个数据系列
        for series_idx, series in enumerate(scaled_data):
            color = self.data_colors[series_idx % len(self.data_colors)]
            for point_idx, y in enumerate(series):
                if y is None:
                    continue
                # 计算柱子X坐标（多系列居中错开）
                base_x = self.plot_x0 + point_idx * x_step
                bar_x = base_x + (series_idx - (num_series - 1)/2) * bar_width
                bar_x = round(bar_x)
                # 柱子高度（从X轴到数据点）
                bar_height = round(self.x_axis_y - y)
                if bar_height > 0:
                    self.display.fill_rect(bar_x, y, round(bar_width), bar_height, color)

        # 刷新显示屏
        self.display.show()
        
class UI():
    def __init__(self):
        self.display = tft_lcd

    def ProgressBar(self, x, y, width, height, progress, color= lcd.WHITE):
        radius = int(height / 2)
        xRadius = x + radius
        yRadius = y + radius
        doubleRadius = 2 * radius
        innerRadius = radius - 2

        self.display.RoundRect(x, y, width, height, radius, color)
        maxProgressWidth = int((width - doubleRadius + 1) * progress / 100)
        self.display.fill_circle(xRadius, yRadius, innerRadius, color)
        self.display.fill_rect(xRadius + 1, y + 2, maxProgressWidth, height - 3, color)
        self.display.fill_circle(xRadius + maxProgressWidth, yRadius, innerRadius, color)

    def stripBar(self, x, y, width, height, progress, dir=1, frame_color=lcd.WHITE, color=lcd.WHITE):

        self.display.rect(x, y, width, height, frame_color)
        if dir:
            Progress = int(progress / 100 * width)
            self.display.fill_rect(x, y, Progress, height, color)
        else:
            Progress = int(progress / 100 * height)
            self.display.fill_rect(x, y + (height - Progress), width, Progress, color)


    def qr_code(self, str, x, y, scale = 4):
        qr = adafruit_miniqr.QRCode(qr_type = 3, error_correct = adafruit_miniqr.L)
        qr.add_data(str.encode())
        qr.make()
        for _y in range(qr.matrix.height):    # each scanline in the height
            for _x in range(qr.matrix.width):
                if qr.matrix[_x, _y]:
                    self.display.fill_rect(_x * scale + x, _y*scale + y, scale,scale, lcd.BLACK)
                else:
                    self.display.fill_rect(_x * scale + x, _y*scale + y, scale,scale, lcd.WHITE)
        gc.collect()
               
class multiScreen():
    def __init__(self, tft_lcd, framelist, w, h):
        self.display = tft_lcd
        self.framelist = framelist
        self.width = w
        self.hight = h
        self.frameCount = len(framelist)
        self.activeSymbol = bytearray([0x00, 0x18, 0x3c, 0x7e, 0x7e, 0x3c, 0x18, 0x00])
        self.inactiveSymbol = bytearray([0x00, 0x0, 0x0, 0x18, 0x18, 0x0, 0x0, 0x00])
        self.SymbolInterval = 1

    def drawScreen(self, index, color = lcd.WHITE):
        self.index = index
        self.display.fill(0)
        self.display.Bitmap(int(64 - self.width / 2), int(0.3 * self.hight), self.framelist[self.index], self.width,
                            self.hight, color)
        SymbolWidth = self.frameCount * 8 + (self.frameCount - 1) * self.SymbolInterval
        SymbolCenter = int(SymbolWidth / 2)
        starX = 64 - SymbolCenter
        for i in range(self.frameCount):
            x = starX + i * 8 + i * self.SymbolInterval
            y = int(1.1 * self.hight) + 8
            if i == self.index:
                self.display.Bitmap(x, y, self.activeSymbol, 8, 8, color)
            else:
                self.display.Bitmap(x, y, self.inactiveSymbol, 8, 8, color)

    def nextScreen(self):
        self.index = (self.index + 1) % self.frameCount
        self.drawScreen(self.index)


class Clock:
    def __init__(self, tft_lcd, x, y, radius, color = lcd.WHITE):  #定义时钟中心点和半径
        self.display = tft_lcd
        self.xc = x
        self.yc = y
        self.r = radius
        self.color = color

    def settime(self):  #设定时间
        t = time.localtime()
        self.hour = t[3]
        self.min = t[4]
        self.sec = t[5]

    def drawDial(self):  #画钟表刻度
        r_tic1 = self.r - 1
        r_tic2 = self.r - 2

        self.display.circle(self.xc, self.yc, self.r, self.color)
        self.display.fill_circle(self.xc, self.yc, 2, self.color)

        for h in range(12):
            at = math.pi * 2.0 * h / 12.0
            x1 = round(self.xc + r_tic1 * math.sin(at))
            x2 = round(self.xc + r_tic2 * math.sin(at))
            y1 = round(self.yc - r_tic1 * math.cos(at))
            y2 = round(self.yc - r_tic2 * math.cos(at))
            self.display.line(x1, y1, x2, y2, self.color)

    def drawHour(self):  #画时针

        r_hour = int(self.r / 10.0 * 5)
        ah = math.pi * 2.0 * ((self.hour % 12) + self.min / 60.0) / 12.0
        xh = int(self.xc + r_hour * math.sin(ah))
        yh = int(self.yc - r_hour * math.cos(ah))
        self.display.line(self.xc, self.yc, xh, yh, self.color)

    def drawMin(self):  #画分针

        r_min = int(self.r / 10.0 * 7)
        am = math.pi * 2.0 * self.min / 60.0

        xm = round(self.xc + r_min * math.sin(am))
        ym = round(self.yc - r_min * math.cos(am))
        self.display.line(self.xc, self.yc, xm, ym, self.color)

    def drawSec(self):  #画秒针

        r_sec = int(self.r / 10.0 * 9)
        asec = math.pi * 2.0 * self.sec / 60.0
        xs = round(self.xc + r_sec * math.sin(asec))
        ys = round(self.yc - r_sec * math.cos(asec))
        self.display.line(self.xc, self.yc, xs, ys, self.color)

    def drawClock(self):  #画完整钟表
        self.drawDial()
        self.drawHour()
        self.drawMin()
        self.drawSec()

    def clear(self):  #清除
        self.display.fill_circle(self.xc, self.yc, self.r, 0)


class Image():
    def __init__(self):
        self.file_type = None

    def load(self, path, invert=0):
        self.invert = invert
        with open(path, 'rb') as file:
            header = file.read(8)
            
            if len(header) < 2:  # 至少需要2字节判断BMP
                self.file_type = 'UNKNOWN FORMAT'
                raise TypeError("Unsupported image format {}".format(self.file_type))
            
            if header[:2] == b'P4':
                self.file_type = 'PBM'
            if header[:2] == b'BM':
                self.file_type = 'BMP'
            if header[:8] == b'\x89PNG\r\n\x1a\n':
                self.file_type = 'PNG'
            if len(header) >= 3 and header[0] == 0xFF and header[1] == 0xD8 and header[2] == 0xFF:
                self.file_type = 'JPEG'
            
            file.seek(0)
            img_arrays = bytearray(file.read())
            if self.file_type == 'PBM':
                fb = self._pbm_decode(img_arrays)

            elif self.file_type == 'BMP':
                fb = self._bmp_decode(img_arrays)
            elif self.file_type == 'PNG':
                w, h, buff = tft_lcd.decode_png(img_arrays)
                fb = framebuf.FrameBuffer(buff, w, h, framebuf.RGB565)            
            else:
                raise TypeError("Unsupported image format {}".format(self.header))
            gc.collect()
            return fb

    def _pbm_decode(self, img_arrays):
        next_value = bytearray()
        pnm_header = []
        stat = True
        index = 3
        while stat:
            next_byte = bytes([img_arrays[index]])
            if next_byte == b"#":
                while bytes([img_arrays[index]]) not in [b"", b"\n"]:
                    index += 1
            if not next_byte.isdigit():
                if next_value:
                    pnm_header.append(int("".join(["%c" % char for char in next_value])))
                    next_value = bytearray()
            else:
                next_value += next_byte
            if len(pnm_header) == 2:
                stat = False
            index += 1
        pixel_arrays = img_arrays[index:]
        if self.invert == 1:
            for i in range(len(pixel_arrays)):
                pixel_arrays[i] = (~pixel_arrays[i]) & 0xff
        return framebuf.FrameBuffer(pixel_arrays, pnm_header[0], pnm_header[1], 3)

    def _bmp_decode(self, img_arrays):

        file_size = int.from_bytes(img_arrays[2:6], 'little')
        offset = int.from_bytes(img_arrays[10:14], 'little')
        width = int.from_bytes(img_arrays[18:22], 'little')
        height = int.from_bytes(img_arrays[22:26], 'little')
        bpp = int.from_bytes(img_arrays[28:30], 'little')
        if bpp != 1:
            raise TypeError("Only support 1 bit color bmp")
        line_bytes_size = (bpp * width + 31) // 32 * 4
        array_size = width * abs(height) // 8
        pixel_arrays = bytearray(array_size)

        if width % 8:
            array_row = width // 8 + 1
        else:
            array_row = width // 8
        array_col = height

        # print("fileSize:{}, offset: {} ".format(file_size, offset))
        # print("width:{}, height: {},bit_count:{},line_bytes_size:{},array_size:{},".format(
        #     width, height, bpp, line_bytes_size, array_size))
        # print('array_col:{},array_row:{}'.format(array_col, array_row))

        for i in range(array_col):
            for j in range(array_row):
                index = -(array_row * (i + 1) - j)
                _offset = offset + i * line_bytes_size + j
                if self.invert == 0:
                    pixel_byte = (~img_arrays[_offset]) & 0xff
                else:
                    pixel_byte = img_arrays[_offset]
                pixel_arrays[index] = pixel_byte

        return framebuf.FrameBuffer(pixel_arrays, width, height, 3)
