# from mpython import *
# # import font.dvsm_21 as dvsm_21

# tft_lcd.clear(lcd.BLACK)
# img = Image()
# fb = img.load('images/B&W/1.png')
# tft_lcd.blit(fb, 200, 80)
# tft_lcd.show()

# frame_colorbuf 绘图API测试
# 水平线和垂直线
# tft_lcd.vline(10, 10, 152, lcd.BLUE)
# tft_lcd.hline(10, 10, 300, lcd.BLUE)
# tft_lcd.vline(310, 10, 152, lcd.BLUE)
# tft_lcd.hline(10, 162, 300, lcd.BLUE)
# 任意两点线段
# tft_lcd.line(0, 0, 320, 172, lcd.WHITE)
# tft_lcd.line(320, 0, 0, 172, lcd.WHITE)
# 矩形和填充矩形
# tft_lcd.rect(20, 20, 280, 132, lcd.RED)
# tft_lcd.fill_rect(50, 50, 70, 70, lcd.RED)
# 圆和填充圆
# tft_lcd.circle(160, 86, 35, lcd.WHITE)
# tft_lcd.fill_circle(160, 86, 25, lcd.RED)     
# 三角形和填充三角形
# tft_lcd.triangle(200, 50, 280, 50, 240, 130, lcd.WHITE)
# tft_lcd.fill_triangle(210, 60, 270, 60, 240, 110, lcd.RED)
# 圆角矩形
# tft_lcd.RoundRect(41, 21, 260, 140, 11, lcd.BLUE)

# 文字显示测试(思源黑体24号)
# tft_lcd.DispChar("中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!中华人民共和国!", 0, -7, lcd.WHITE, True)
# tft_lcd.show()
# font文件夹下内置的数码管及dvssm字体显示测试,字体由font-to-py工具生成
# tft_lcd.DispChar_font(dvsm_21, "12:34:56", 0, 60, lcd.RED)


# from mpython import *
# tft_lcd.clear(lcd.BLACK)
# w, h, buff = tft_lcd.decode_png_internal(0)
# fb = frame_colorbuf.frame_colorBuffer(buff, w, h, frame_colorbuf.RGB565)
# tft_lcd.blit(fb, 10, 10)
# tft_lcd.show()

import lcd
from mpython import *


# gui = UI(tft_lcd)

# tft_lcd.clear(lcd.BLACK)
# gui.ProgressBar(10, 10, 200, 20, 20, lcd.RED)
# gui.stripBar(10, 40, 200, 20, 50, dir=1, color=lcd.BLUE)
# tft_lcd.clear(lcd.BLACK)
# gui.qr_code("Hello, mPython!", 10, 10)
# gui.qr_code("Hello, mPython!", 10, 10, scale=4)
# tft_lcd.show()

import math

class MicroChart:
    """
    MicroPython 轻量级图表类，支持折线图、柱状图
    适配 SSD1306 (OLED)、ST7735 (TFT) 等显示屏
    """
    def __init__(self, display = tft_lcd, x0=0, y0=0, width=200, height=100, 
                 bg_color=lcd.BLACK, axis_color=lcd.WHITE, data_colors=[lcd.RED, lcd.GREEN, lcd.BLUE], padding=10):
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
        self.plot_height = self.height - self.padding  # 绘图区高度（Y轴缩放基准）
        self.x_axis_y = self.plot_y0 + self.plot_height  # X轴Y坐标

    def _scale_data(self, data):
        """
        重写：按Y轴绘图区高度缩放数据，超出范围值强制限定在高度边界内
        :param data: 二维列表 [[系列1数据], [系列2数据], ...]
        :return: 缩放后的数据、X轴步长、数据极值(min/max)
        """
        # 扁平化数据找极值（忽略None）
        flat_data = [val for series in data for val in series if val is not None]
        if not flat_data:
            return [], 0, 0, 0  # 空数据
        
        min_val = min(flat_data)
        max_val = max(flat_data)
        
        # 避免极值相同导致除零，重置缩放基准
        value_range = max_val - min_val
        if value_range == 0:
            value_range = 1
            max_val += 1
            min_val -= 1

        # 核心：按Y轴绘图区高度计算缩放比例
        scale_ratio = self.plot_height / value_range  # 每单位数据对应的像素高度

        scaled_data = []
        for series in data:
            scaled_series = []
            for val in series:
                if val is None:
                    scaled_series.append(None)
                    continue
                
                # 步骤1：计算数据相对最小值的偏移量
                relative_val = val - min_val
                # 步骤2：按高度缩放
                scaled_val = relative_val * scale_ratio
                # 步骤3：强制限定范围（0 ≤ scaled_val ≤ 绘图区高度）
                scaled_val = max(0, min(self.plot_height, scaled_val))
                # 步骤4：转换为屏幕坐标（屏幕Y轴向下，需反转）
                screen_y = self.x_axis_y - scaled_val
                scaled_series.append(round(screen_y))  # 确保坐标为整数
            scaled_data.append(scaled_series)

        # 计算X轴每个数据点的像素步长
        num_points = max(len(series) for series in data)
        x_step = self.plot_width / (num_points - 1) if num_points > 1 else 1

        return scaled_data, x_step, min_val, max_val

    def draw_background(self):
        """绘制图表背景（填充矩形）"""
        self.display.fill_rect(self.x0, self.y0, self.width, self.height, self.bg_color)

    def draw_axis(self, show_ticks=True):
        """绘制坐标轴（可选刻度）- 修复浮点数坐标问题"""
        # 绘制Y轴（垂直）- 确保坐标为整数
        y_axis_x = round(self.plot_x0 - 5)
        self.display.line(
            y_axis_x, 
            round(self.y0), 
            y_axis_x, 
            round(self.x_axis_y), 
            self.axis_color
        )
        # 绘制X轴（水平）- 确保坐标为整数
        self.display.line(
            round(self.x0), 
            round(self.x_axis_y), 
            round(self.x0 + self.width), 
            round(self.x_axis_y), 
            self.axis_color
        )

        # 绘制刻度（可选）
        if show_ticks:
            # Y轴刻度（5个）- 修复浮点数坐标
            tick_count = 5
            tick_step = self.plot_height / (tick_count - 1)
            for i in range(tick_count):
                tick_y = round(self.x_axis_y - i * tick_step)  # 转为整数
                self.display.line(
                    round(y_axis_x - 3), 
                    tick_y, 
                    y_axis_x, 
                    tick_y, 
                    self.axis_color
                )
            # X轴刻度（5个）- 修复浮点数坐标
            tick_step_x = self.plot_width / (tick_count - 1)
            for i in range(tick_count):
                tick_x = round(self.plot_x0 + i * tick_step_x)  # 转为整数
                self.display.line(
                    tick_x, 
                    round(self.x_axis_y), 
                    tick_x, 
                    round(self.x_axis_y + 3), 
                    self.axis_color
                )

    def draw_grid(self, grid_color=None):
        """绘制网格线（可选）- 修复浮点数坐标问题"""
        if grid_color is None:
            # 单色屏用浅色调，彩色屏用灰度
            grid_color = self.axis_color // 2 if self.axis_color > 1 else 1
        # 水平网格线 - 修复浮点数坐标
        tick_count = 5
        tick_step = self.plot_height / (tick_count - 1)
        for i in range(tick_count):
            y = round(self.x_axis_y - i * tick_step)  # 转为整数
            self.display.line(
                round(self.plot_x0), 
                y, 
                round(self.plot_x0 + self.plot_width), 
                y, 
                grid_color
            )
        # 垂直网格线 - 修复浮点数坐标
        tick_step_x = self.plot_width / (tick_count - 1)
        for i in range(tick_count):
            x = round(self.plot_x0 + i * tick_step_x)  # 转为整数
            self.display.line(
                x, 
                round(self.plot_y0), 
                x, 
                round(self.x_axis_y), 
                grid_color
            )

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
                # 计算当前点X坐标并转为整数
                x = round(self.plot_x0 + point_idx * x_step)
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
        # 计算柱子宽度（多系列错开显示）- 确保宽度为整数
        bar_width = (x_step / (num_series + 1)) * 0.8
        bar_width = max(1, round(bar_width))  # 转为整数，最小宽度1像素

        # 绘制每个数据系列
        for series_idx, series in enumerate(scaled_data):
            color = self.data_colors[series_idx % len(self.data_colors)]
            for point_idx, y in enumerate(series):
                if y is None:
                    continue
                # 计算柱子X坐标并转为整数（多系列居中错开）
                base_x = self.plot_x0 + point_idx * x_step
                bar_x = round(base_x + (series_idx - (num_series - 1)/2) * bar_width)
                # 柱子高度（从X轴到数据点）- 确保高度为整数
                bar_height = round(self.x_axis_y - y)
                if bar_height > 0:
                    self.display.fill_rect(bar_x, y, bar_width, bar_height, color)

        # 刷新显示屏
        self.display.show()

chart = MicroChart()
line_data = [[0]*100]
chart.draw_line_chart(line_data, show_grid=False)

# import random
import audio
tft_lcd.clear(lcd.BLACK)
tft_lcd.show()
i = 0
chart.draw_axis()
while True:
    value = audio.loudness() # random.randint(0, 100)
    for j in range(99):
        line_data[0][j] = line_data[0][j+1]
    line_data[0][99] = value # random.randint(0, 50)
    chart.draw_line_chart(line_data, show_grid=False)
    i = (i + 1) % len(line_data[0])
    time.sleep_ms(50)
    
bar_data = [[3, 8, 5, 10], [6, 4, 9, 7], [2, 7, 4, 9]]
chart.draw_bar_chart(bar_data)
tft_lcd.show()