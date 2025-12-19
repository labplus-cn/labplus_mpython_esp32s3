# gui for mpython
# MIT license; Copyright (c) 2019 Zhang Kaihua(apple_eat@126.com)
import time, math, gc, lcd
import framebuf
import adafruit_miniqr
from mpython import tft_lcd

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

class MicroExcelTable:
    """
    MicroPython 轻量级Excel表格类（含行列双表头）
    特性：行列表头仅初始化绘制，支持单单元格/整数据区更新，适配嵌入式显示屏
    """
    def __init__(self, display = tft_lcd, x0=0, y0=0, 
                 cols=3, rows=4, cell_width=85, cell_height=30,
                 # 列表头配置
                 col_header_text_color=lcd.RED,
                 # 列表头配置（新增）
                 row_header_width=60, row_header_text_color=lcd.RED,
                 # 数据区配置
                 data_text_color=lcd.WHITE,
                 border_color=lcd.GREEN, show_border=True,
                 font_size=(16, 16),
                 # 行列表头交叉区文本（左上角）
                 cross_text=""):
        """
        初始化表格
        :param display: 显示屏对象（需实现text/fill_rect/line/show方法）
        :param x0/y0: 表格左上角坐标
        :param cols/rows: 表格列数/数据行数（不含表头）
        :param cell_width/cell_height: 数据区单元格宽/高（像素）
        :param text_color: 列表头文字色
        :param row_header_width: 列表头宽度（像素）
        :param text_color: 列表头文字色
        :param text_color: 数据区文字色
        :param border_color: 边框色
        :param show_border: 是否显示边框
        :param font_size: 字体尺寸 (width, height)
        :param cross_text: 行列表头交叉区（左上角）显示文本
        """
        # 基础参数
        self.display = display
        self.bg_color = self.display.background_color()
        self.x0 = x0
        self.y0 = y0
        self.cols = cols
        self.rows = rows  # 数据行数（不含表头）
        self.cell_w = cell_width
        self.cell_h = cell_height
        self.font_w, self.font_h = font_size
        self.show_border = show_border
        self.border_color = border_color
        self.cross_text = cross_text  # 交叉区文本

        # 行列表头配置（新增）
        self.row_header_w = row_header_width  # 列表头宽度
        self.col_header_h = self.cell_h       # 列表头高度
        self.row_header_text_color = row_header_text_color
        self.col_header_text = col_header_text_color

        # 数据区配置
        self.data_text = data_text_color

        # 关键坐标计算（适配行列表头）
        ## 交叉区（左上角）坐标
        self.cross_x0 = self.x0
        self.cross_y0 = self.y0
        self.cross_w = self.row_header_w
        self.cross_h = self.col_header_h

        ## 列表头区域（交叉区右侧）
        self.col_header_x0 = self.cross_x0 + self.cross_w
        self.col_header_y0 = self.y0
        self.col_header_w = self.cols * self.cell_w
        self.col_header_h = self.col_header_h

        ## 列表头区域（交叉区下侧）
        self.row_header_x0 = self.x0
        self.row_header_y0 = self.cross_y0 + self.cross_h
        self.row_header_w = self.row_header_w
        self.row_header_h = self.rows * self.cell_h

        ## 数据区区域（行列表头右侧/下侧）
        self.data_x0 = self.col_header_x0  # 数据区起始X（列表头右侧）
        self.data_y0 = self.row_header_y0  # 数据区起始Y（列表头下侧）
        self.data_w = self.cols * self.cell_w
        self.data_h = self.rows * self.cell_h

        # 表格总尺寸
        self.table_total_w = self.row_header_w + self.cols * self.cell_w
        self.table_total_h = self.col_header_h + self.rows * self.cell_h

        # 缓存：表头数据和绘制状态
        self._col_header_data = None
        self._row_header_data = None
        self._headers_drawn = False  # 行列表头是否已绘制

        # 缓存：当前数据
        self._current_data = [["" for _ in range(self.cols)] for _ in range(self.rows)]

    def _draw_border(self):
        """绘制表格边框（适配行列表头）"""
        if not self.show_border:
            return
        # 表格外边框
        self.display.rect(self.x0, self.y0, self.table_total_w, self.table_total_h, self.border_color)
        
        # 交叉区分隔线（行列表头分隔）
        ## 交叉区右侧线（列表头左边界）
        self.display.line(self.col_header_x0, self.y0, self.col_header_x0, self.y0 + self.table_total_h, self.border_color)
        ## 交叉区下侧线（列表头上边界）
        self.display.line(self.x0, self.row_header_y0, self.x0 + self.table_total_w, self.row_header_y0, self.border_color)

        # 列表头内部分隔线
        for col in range(1, self.cols):
            x = self.col_header_x0 + col * self.cell_w
            self.display.line(x, self.col_header_y0, x, self.col_header_y0 + self.col_header_h, self.border_color)

        # 列表头内部分隔线
        for row in range(1, self.rows):
            y = self.row_header_y0 + row * self.cell_h
            self.display.line(self.row_header_x0, y, self.row_header_x0 + self.row_header_w, y, self.border_color)

        # 数据区内部分隔线
        ## 列分隔线
        for col in range(1, self.cols):
            x = self.data_x0 + col * self.cell_w
            self.display.line(x, self.data_y0, x, self.data_y0 + self.data_h, self.border_color)
        ## 行分隔线
        for row in range(1, self.rows):
            y = self.data_y0 + row * self.cell_h
            self.display.line(self.data_x0, y, self.data_x0 + self.data_w, y, self.border_color)

    def _draw_cell_text(self, x, y, text, text_color, cell_w, cell_h):
        """绘制单元格文字（自动截断，居中对齐）"""
        # self.display.fill_rect(x, y, cell_w, cell_h, bg_color)
        # max_chars = (cell_w - 2) // self.font_w  # 留2像素边距
        # if len(text) > max_chars:
        #     text = text[:max_chars-1] + "."
        text_x = x + (cell_w - len(text) * self.font_w) // 2 -13
        text_y = y + (cell_h - self.font_h) // 2 -15
        # self.display.text(text, text_x, text_y, text_color)
        # import font.dvsmb_16 as dvsmb_16
        # self.display.DispChar_font(dvsmb_16, text, text_x, text_y, text_color, auto_wrap = False)
        tft_lcd.DispChar(text, text_x, text_y, text_color, auto_wrap = False)

    def _draw_cell_border(self, x, y, cell_w, cell_h):
        """绘制单个单元格边框"""
        if not self.show_border:
            return
        self.display.line(x, y, x + cell_w, y, self.border_color)       # 上
        self.display.line(x, y + cell_h, x + cell_w, y + cell_h, self.border_color)  # 下
        self.display.line(x, y, x, y + cell_h, self.border_color)       # 左
        self.display.line(x + cell_w, y, x + cell_w, y + cell_h, self.border_color)  # 右

    def draw_headers(self, col_header_data, row_header_data):
        """
        绘制行列双表头（仅初始化/手动刷新时调用）
        :param col_header_data: 列表头列表 e.g. ["温度", "湿度", "气压"]
        :param row_header_data: 列表头列表 e.g. ["设备1", "设备2", "设备3"]
        """
        # 校验维度
        if len(col_header_data) != self.cols:
            raise ValueError(f"列表头长度需等于列数({self.cols})")
        if len(row_header_data) != self.rows:
            raise ValueError(f"列表头长度需等于数据行数({self.rows})")
        
        # 缓存表头数据
        self._col_header_data = col_header_data
        self._row_header_data = row_header_data

        # 1. 绘制交叉区（左上角）
        self._draw_cell_text(
            self.cross_x0, self.cross_y0, self.cross_text,
            self.col_header_text,
            self.cross_w, self.cross_h
        )

        # 2. 绘制列表头（交叉区右侧）
        for col, text in enumerate(col_header_data):
            x = self.col_header_x0 + col * self.cell_w
            self._draw_cell_text(
                x, self.col_header_y0, text,
                self.col_header_text,
                self.cell_w, self.col_header_h
            )

        # 3. 绘制列表头（交叉区下侧）
        for row, text in enumerate(row_header_data):
            y = self.row_header_y0 + row * self.cell_h
            self._draw_cell_text(
                self.row_header_x0 + 5, y, text,
                self.row_header_text_color,
                self.row_header_w, self.cell_h
            )

        # 4. 绘制边框（仅首次）
        if not self._headers_drawn:
            self._draw_border()
            self._headers_drawn = True

        # 刷新显示屏
        # self.display.show()

    def _clear_data_area(self):
        """清除数据区（仅清除，不刷新）"""
        self.display.fill_rect(self.data_x0, self.data_y0, self.data_w, self.data_h, self.bg_color)
        # 重绘数据区边框
        if self.show_border:
            self.display.rect(self.data_x0, self.data_y0, self.data_w, self.data_h, self.border_color)
            # 列分隔线
            for col in range(1, self.cols):
                x = self.data_x0 + col * self.cell_w
                self.display.line(x, self.data_y0, x, self.data_y0 + self.data_h, self.border_color)
            # 行分隔线
            for row in range(1, self.rows):
                y = self.data_y0 + row * self.cell_h
                self.display.line(self.data_x0, y, self.data_x0 + self.data_w, y, self.border_color)

    def update_data(self, new_data):
        """更新整个数据区（不刷新表头）"""
        if len(new_data) > self.rows:
            raise ValueError(f"数据行数不能超过{self.rows}")
        for row_data in new_data:
            if len(row_data) > self.cols:
                raise ValueError(f"每行数据列数不能超过{self.cols}")

        self._clear_data_area()

        # 绘制新数据
        for row, row_data in enumerate(new_data):
            row_data = row_data + [""] * (self.cols - len(row_data))
            self._current_data[row] = row_data
            for col, text in enumerate(row_data):
                x = self.data_x0 + col * self.cell_w
                y = self.data_y0 + row * self.cell_h
                self._draw_cell_text(x, y, str(text), self.data_text, self.cell_w, self.cell_h)

    def update_cell(self, row, col, text):
        """更新单个单元格（不刷新表头）"""
        if row < 0 or row >= self.rows:
            raise ValueError(f"row cell index must in: 0~{self.rows-1}")
        if col < 0 or col >= self.cols:
            raise ValueError(f"col cell index must in: 0~{self.cols-1}")
        
        text = str(text) if text is not None else ""
        # 计算单元格坐标（适配列表头偏移）
        cell_x = self.data_x0 + col * self.cell_w
        cell_y = self.data_y0 + row * self.cell_h

        # 清除单元格背景 + 重绘边框 + 绘制文字
        self.display.fill_rect(cell_x, cell_y, self.cell_w, self.cell_h, self.bg_color)
        self._draw_cell_border(cell_x, cell_y, self.cell_w, self.cell_h)
        self._draw_cell_text(cell_x, cell_y, text, self.data_text, self.cell_w, self.cell_h)

        # 更新缓存
        self._current_data[row][col] = text

    def refresh_headers(self):
        """手动刷新行列表头"""
        if self._col_header_data is None or self._row_header_data is None:
            raise RuntimeError("请先调用draw_headers绘制表头")
        self.draw_headers(self._col_header_data, self._row_header_data)

    def clear_all(self):
        """清空整个表格"""
        self.display.fill_rect(self.x0, self.y0, self.table_total_w, self.table_total_h, self.bg_color)
        self._headers_drawn = False
        self._current_data = [["" for _ in range(self.cols)] for _ in range(self.rows)]
        # self.display.show()
    
class MicroChart:
    """
    MicroPython 轻量级图表类，支持折线图、柱状图
    适配 SSD1306 (OLED)、ST7735 (TFT) 等显示屏
    """
    def __init__(self, display = tft_lcd, x0=0, y0=0, width=200, height=100, val_min = 0, val_max = 100,
                 bg_color=lcd.BLACK, axis_color=lcd.WHITE, data_colors=[lcd.RED, lcd.GREEN, lcd.BLUE], padding=10):
        """
        初始化图表
        :param display: 显示屏对象（如SSD1306_I2C/ST7735）
        :param x0/y0: 图表左上角坐标
        :param width/height: 图表尺寸
        :param val_min val_max: 待显示数据范围
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
        self.min_val = val_min
        self.max_val = val_max
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

    def _scale_data(self, data, chat_type):
        """
        数据缩放：将原始数据映射到绘图区像素范围
        :param data: 二维列表 [[系列1数据], [系列2数据], ...]
        :return: 缩放后的数据、X轴步长、数据极值(min/max)
        """
        # 扁平化数据找极值（忽略None）
        flat_data = [val for series in data for val in series if val is not None]
        if not flat_data:
            return [], 0, 0, 0  # 空数据
        

        # 避免极值相同导致除零，重置缩放基准
        value_range = self.max_val - self.min_val
        if value_range == 0:
            value_range = 1
            self.max_val += 1
            self.min_val -= 1

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
                relative_val = val - self.min_val
                # 步骤2：按高度缩放
                scaled_val = relative_val * scale_ratio
                # 步骤3：强制限定范围（0 ≤ scaled_val ≤ 绘图区高度）
                scaled_val = max(0, min(self.plot_height, scaled_val))
                # 步骤4：转换为屏幕坐标（屏幕Y轴向下，需反转）
                screen_y = self.x_axis_y - scaled_val
                scaled_series.append(round(screen_y))  # 确保坐标为整数
            scaled_data.append(scaled_series)

        # 计算X轴每个数据点的像素步长
        if chat_type == 1:
            num_points = max(len(series) for series in data)
            x_step = self.plot_width / (num_points - 1) if num_points > 1 else 1
        if chat_type == 2:
            num_points = len(data[0]) * len(data) + len(data) -1
            x_step = (int)(self.plot_width / (num_points) if num_points > 0 else 1)

        return scaled_data, x_step, self.min_val, self.max_val

    def draw_background(self):
        """绘制图表背景（填充矩形）"""
        self.display.fill_rect(self.x0, self.y0, self.width + 1, self.height + 1, self.bg_color)

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
        scaled_data, x_step, _, _ = self._scale_data(data, chat_type = 1)

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
        # self.display.show()

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
        scaled_data, x_step, _, _ = self._scale_data(data, chat_type=2)

        # 绘制每个数据系列
        bar_x = self.plot_x0
        for series_idx, series in enumerate(scaled_data):
            for point_idx, y in enumerate(series):
                color = self.data_colors[point_idx % len(self.data_colors)]
                if y is None:
                    continue
                # 计算柱子X坐标并转为整数（多系列居中错开）
                bar_x = bar_x + x_step
                # 柱子高度（从X轴到数据点）- 确保高度为整数
                bar_height = round(self.x_axis_y - y)
                if bar_height > 0:
                    self.display.fill_rect(bar_x, y, x_step, bar_height, color)

                if point_idx == len(series) - 1: # 每两组数据间加空柱
                    bar_x = bar_x + x_step
                    self.display.fill_rect(bar_x, y, x_step, 1, self.bg_color)
        
        bar_x = bar_x + len(series) * x_step       
        # 刷新显示屏
        # self.display.show()
 