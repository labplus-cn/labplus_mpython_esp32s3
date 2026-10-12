import gc
import _thread
import time

try:
    from firmware_language import IS_ENGLISH
except ImportError:
    IS_ENGLISH = False  # Standalone driver copies default to Chinese.

try:
    from mpython_i18n import print_i18n as _print_i18n
except ImportError:
    def _print_i18n(chinese, english, *args, **kwargs):
        print(english if IS_ENGLISH else chinese, *args, **kwargs)

DISPLAY_WIDTH = 544
DISPLAY_HEIGHT = 368
DEFAULT_MODE = 0 # 默认UI
SENSOR_MODE = -1 # 摄像头
FACE_DETECTION_MODE = 1
OBJECT_RECOGNIZATION_MODE = 2 
HAND_DETECTION = 3
HAND_KEYPOINT_CLASS = 4
QRCODE_MODE = 5
PERSON_DETECTION = 6
FALL_DETECTION = 7 
LICENSE_PLATE_RECOGNITION = 8
CANNY_FIND_EDGES = 9
PERSON_KEYPOINT_DETECT = 10

PERSON_KEYPOINT_DETECT_PLUS = 11 
# COLOR_STATISTICS_MODE = 13 # 颜色的统计信息
# COLOR_EXTRACTO_MODE = 14 # LAB颜色提取器
APRILTAG_MODE = 15
COLOE_MODE = 16
SPEECH_RECOGNIZATION_MODE = 17
SELF_LEARNING_CLASSIFIER_MODE = 18 # 自学习
GUIDEPOST_MODE = 19 #清华教材交通标志识别
KPU_MODEL_MODE = 20 #自定义模型 

HAND_RECOGNIZATION_MODE = 21
BARCODE_MODE = 23

DYNAMIC_GESTURE = 25 #动态手势
FACE_LANDMARK = 26  # 人脸关键点
FACE_LANDMARK_LIVING_BODY = 27
FACE_LANDMARK_EXPRESSION = 28

COLOR_OBJECT_COUNT_MODE = 29
LAB_COLOR_OBJECT_COUNT_MODE = 22 #LAB


FACE_REG_MODE = 30 # 人脸注册
FACE_RECOGNITION_MODE = 31 #人脸识别
CLASSIFY_MODEL_MODE = 32 # 分类模型
DETECT_MODEL_MODE = 33  # 检测模型
HAND_KEYPOINT = 34

FACTORY_MODE = 99 

# OBJ_TYPE = ['飞机','自行车','鸟','船','瓶子','公交车','汽车','猫','椅子','奶牛','餐桌','狗','屋子','摩托','人','盆栽','羊','沙发','火车','电视']

YOLO80_ZH = ["人","自行车","汽车","摩托车","飞机","公共汽车","火车","卡车","船","交通灯","消防栓","停车标志","泊车计时器","长椅","鸟","猫","狗","马","绵羊","奶牛","大象","熊","斑马","长颈鹿","背包","雨伞","手提包","领带","手提箱","飞盘","滑雪板","单板滑雪板","运动球","风筝","棒球棒","棒球手套","滑板","冲浪板","网球拍","瓶子","酒杯","杯子","叉子","刀","勺子","碗","香蕉","苹果","三明治","橙子","西兰花","胡萝卜","热狗","披萨","甜甜圈","蛋糕","椅子","沙发","盆栽","床","餐桌","厕所","电视","笔记本电脑","鼠标","遥控器","键盘","手机","微波炉","烤箱","烤面包机", "水槽","冰箱","书籍","时钟","花瓶","剪刀","泰迪熊","吹风机","牙刷"]
YOLO80_EN = ["person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair","couch","potted plant","bed","dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven","toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"]
HAND_KEYPOINT_CLASS_GESTURE_ZH = ['握拳','五指','手枪','比心','一','六','三','点赞','胜利手势']
HAND_KEYPOINT_CLASS_GESTURE_EN = ['fist','five','gun','love','one','six','three','thumbUp','yeah']
FACE_LANDMARK_EXPRESSION_ZH = ['正常','开心','伤心','惊讶','生气']
FACE_LANDMARK_EXPRESSION_EN = ['normal','happy','sad','surprised','angry']
DYNAMIC_GESTURE_STR_ZH = ['上','下','左','右']
DYNAMIC_GESTURE_STR_EN = ['up','down','left','right']


MODE_ZH = ['默认','数字识别','物体识别','人脸检测','人脸识别','自学习分类','颜色识别','二维码识别','语音识别','交通标志识别','KPU自定义模型','寻找色块识别','图像处理','LAB颜色提取器','AprilTag']
MODE_EN = ['default','digit recognition','object recognition','face detection','face recognition','self-learning classification','color recognition','QR code recognition','speech recognition','traffic sign recognition','KPU custom model','color block recognition','image processing','LAB color extractor','AprilTag']

USE_ENGLISH_VOICE = IS_ENGLISH
YOLO80_LABELS = []
HAND_KEYPOINT_CLASS_GESTURE = []
FACE_LANDMARK_EXPRESSION_LABELS = []
DYNAMIC_GESTURE_STR = []
MODE = []


def set_voice_language(use_english):
    """Switch result labels without invalidating existing list references."""
    global USE_ENGLISH_VOICE
    USE_ENGLISH_VOICE = bool(use_english)
    if USE_ENGLISH_VOICE:
        labels = (YOLO80_EN, HAND_KEYPOINT_CLASS_GESTURE_EN,
                  FACE_LANDMARK_EXPRESSION_EN, DYNAMIC_GESTURE_STR_EN, MODE_EN)
    else:
        labels = (YOLO80_ZH, HAND_KEYPOINT_CLASS_GESTURE_ZH,
                  FACE_LANDMARK_EXPRESSION_ZH, DYNAMIC_GESTURE_STR_ZH, MODE_ZH)
    for target, source in zip(
            (YOLO80_LABELS, HAND_KEYPOINT_CLASS_GESTURE,
             FACE_LANDMARK_EXPRESSION_LABELS, DYNAMIC_GESTURE_STR, MODE), labels):
        target[:] = source


set_voice_language(USE_ENGLISH_VOICE)

AI ={
    'reset':[0xff, 0x01, 0x02],
    'sw_mode':[],
    'config':[0x99,0x01,0x02],
    'lcd':[0x10,0x01,0x02],
    'image':[0x64,0x01],
    'sensor':[0x64,0x01],
    'DEFAULT_MODE':[DEFAULT_MODE,0x01],
    'light':[],
    'button':[],
    'button_A':[],
    'button_B':[],
    'FACE_DETECTION_MODE':[FACE_DETECTION_MODE,0x01],
    'OBJECT_RECOGNIZATION_MODE':[OBJECT_RECOGNIZATION_MODE,0x01],
    'HAND_DETECTION':[HAND_DETECTION,0x01],
    'HAND_RECOGNIZATION_MODE':[HAND_RECOGNIZATION_MODE,0x01],
    'HAND_KEYPOINT_CLASS':[HAND_KEYPOINT_CLASS,0x01],
    'LICENSE_PLATE_RECOGNITION':[LICENSE_PLATE_RECOGNITION,0x01],
    'PERSON_DETECTION':[PERSON_DETECTION,0x01],
    'PERSON_KEYPOINT_DETECT':[PERSON_KEYPOINT_DETECT,0x01],
    'PERSON_KEYPOINT_DETECT_PLUS':[PERSON_KEYPOINT_DETECT_PLUS,0x01],
    'FACE_LANDMARK':[FACE_LANDMARK,0x01],
    'FACE_LANDMARK_LIVING_BODY':[FACE_LANDMARK_LIVING_BODY,0x01],
    'FACE_LANDMARK_EXPRESSION':[FACE_LANDMARK_EXPRESSION,0x01],
    'QRCODE_MODE':[QRCODE_MODE,0x01],
    'BARCODE_MODE':[BARCODE_MODE,0x01],
    'FALL_DETECTION':[FALL_DETECTION,0x01],
    'FACE_RECOGNITION_MODE':[FACE_RECOGNITION_MODE,0x01],
    'LAB_COLOR_OBJECT_COUNT_MODE':[LAB_COLOR_OBJECT_COUNT_MODE,0x01],
    'COLOR_OBJECT_COUNT_MODE':[COLOR_OBJECT_COUNT_MODE,0x01],
    'DYNAMIC_GESTURE':[DYNAMIC_GESTURE,0x01],
    'CLASSIFY_MODEL_MODE':[CLASSIFY_MODEL_MODE,0x01],
    'DETECT_MODEL_MODE':[DETECT_MODEL_MODE,0x01],
    'kpu_model_yolo':[0x10,0x01,0x02,0x03],
    'factory':[99,0x01,0x02,0x03,0x04,0x05]
}


def CheckCode(tmp):
    sum = 0
    for i in range(len(tmp)):
        sum += tmp[i]
    return sum & 0xff


DEBUG = False  # 增加全局debug控制变量
MAX_BUF_SIZE = 1024  # 缓冲区最大字节数

def uart_handle(uart):
    rx_buf = bytearray()

    # 读取 UART 数据
    if uart.any():
        data = uart.read()
        if data:
            if DEBUG:
                print(f"[UART RX] {data.hex(' ').upper()}")
            rx_buf.extend(data)

            # 缓冲区保护
            if len(rx_buf) > MAX_BUF_SIZE:
                if DEBUG:
                    _print_i18n(f"[UART] 缓冲区超限({len(rx_buf)} > {MAX_BUF_SIZE})，清空",
                                f"[UART] RX buffer overflow ({len(rx_buf)} > {MAX_BUF_SIZE}); cleared")
                rx_buf = bytearray()

    while True:
        # 至少要有包头(2字节) + 命令类型(1字节)
        if len(rx_buf) < 3:
            break

        # 检查包头
        if not (rx_buf[0] == 0xBB and rx_buf[1] == 0xAA):
            if DEBUG:
                _print_i18n(f"[UART] 丢弃无效字节: 0x{rx_buf[0]:02X}",
                            f"[UART] Dropping invalid byte: 0x{rx_buf[0]:02X}")
            rx_buf = rx_buf[1:]
            continue

        cmd_type = rx_buf[2]

        # 命令 0x01: 固定长度 2(包头) + 1(命令) + 2 +6(数据) + 1(校验) = 12字节
        if cmd_type == 0x01:
            total_len = 12
            if len(rx_buf) < total_len:
                if DEBUG:
                    _print_i18n("[UART] 等待 0x01 包数据到齐", "[UART] Waiting for 0x01 packet data")
                break
            pkt = rx_buf[:total_len]
            checksum = CheckCode(pkt[:11])  # 校验包头+命令+数据(共11字节)
            if DEBUG:
                _print_i18n(f"[UART] 解析到 0x01 包: {pkt.hex(' ').upper()}",
                            f"[UART] Parsed 0x01 packet: {pkt.hex(' ').upper()}")
            if checksum == pkt[11]:
                if DEBUG:
                    _print_i18n("[UART] 校验成功", "[UART] Checksum passed")
                return pkt  # 返回完整包
            else:
                if DEBUG:
                    _print_i18n(f"[UART] 校验失败 (计算={checksum:02X}, 接收={pkt[11]:02X})",
                                f"[UART] Checksum failed (calculated={checksum:02X}, received={pkt[11]:02X})")
            rx_buf = rx_buf[total_len:]

        # 命令 0x02:  2(包头) + 3(命令) + 15(固定头) +1(字符长度) + N(字符串) + 1(校验)
        elif cmd_type == 0x02:
            if len(rx_buf) < 21:  # 至少要有包头+命令+固定头
                if DEBUG:
                    _print_i18n("[UART] 等待 0x02 头部数据到齐", "[UART] Waiting for 0x02 header data")
                break
            str_len = rx_buf[20]  # 第21字节，索引20
            total_len = 21 + str_len + 1
            if len(rx_buf) < total_len:
                if DEBUG:
                    _print_i18n(f"[UART] 等待 0x02 字符串数据到齐 (len={str_len})",
                                f"[UART] Waiting for 0x02 string data (len={str_len})")
                break
            pkt = rx_buf[:total_len]
            if pkt[-1] != 0xAB:
                if DEBUG:
                    _print_i18n(f"[UART] 0x02 包尾校验失败 (期望=AB, 接收={pkt[-1]:02X})，丢弃第一个字节",
                                f"[UART] Invalid 0x02 packet terminator (expected=AB, received={pkt[-1]:02X}); dropping first byte")
                rx_buf = rx_buf[1:]
                continue
            if DEBUG:
                _print_i18n("[UART] 解析到 0x02 包", "[UART] Parsed 0x02 packet")
                print(pkt)
            rx_buf = rx_buf[total_len:]
        
            return pkt  # 返回完整包

        else:
            if DEBUG:
                _print_i18n(f"[UART] 未知命令 0x{cmd_type:02X}，丢弃第一个字节",
                            f"[UART] Unknown command 0x{cmd_type:02X}; dropping first byte")
            rx_buf = rx_buf[1:]

            return None  # 无有效包返回
        

def AI_Uart_CMD(uart, cmd=0, cmd_type=0, cmd_data=[0, 0, 0, 0, 0, 0, 0, 0]):
    gc.collect()
    data_type = 0x01
    check_sum = 0
    CMD = [0xAA, 0xBB, data_type, cmd, cmd_type]
    CMD.extend(cmd_data)
    for i in range(8-len(cmd_data)):
        CMD.append(0)
    for i in range(len(CMD)):
        check_sum = check_sum+CMD[i]

    CMD.append(check_sum & 0xFF)
    # print(CMD)
    uart.write(bytes(CMD))


def AI_Uart_CMD_String(uart=None, cmd=0xfe, cmd_type=0xfe, cmd_data=[0, 0, 0], str_len=0, str_buf=''):
    gc.collect()
    check_sum = 0
    CMD = [0xAA, 0xBB, 0x02, cmd, cmd_type]
    CMD.extend(cmd_data)
    for i in range(3-len(cmd_data)):
        CMD.append(0)
    for i in range(len(CMD)):
        check_sum = check_sum+CMD[i]
  
    str_temp = bytes(str_buf, 'utf-8')
    str_len = len(str_temp)
    
    for i in range(len(str_temp)):
        check_sum = check_sum + str_temp[i]  

    CMD = bytes(CMD) + bytes([str_len]) + str_temp + bytes([check_sum & 0xFF])
    uart.write(CMD)

def print_x16(date):
    for i in range(len(date)):
        print('{:2x}'.format(date[i]),end=' ')
    print('')

def hammingWeight(n):
    '''
    判断16bit中哪一位为1
    '''
    ans = 0
    for i in range(16):
        if n & 1 == 1:
          ans = i
        n >>= 1
    return ans

class TASK:
    """创建新线程并循环运行指定函数"""
    def __init__(self, func=lambda: None, sec=-1, *args, **kwargs):
        """
        * func 需要循环运行的函数
        * sec  每次循环的延迟，负数则只运行一次
        * args kwagrs 函数的参数
        * enable 使能运行
        """
        self._thread = _thread
        self.sec = sec
        self.func = func
        self.args, self.kwargs = args, kwargs
        self.enable = True
        self.lock = self._thread.allocate_lock()
        self.stop_lock = self._thread.allocate_lock()
        self.lock.acquire()
        self.stop_lock.acquire()
        self._thread.stack_size(8192)
        self.thread_id = self._thread.start_new_thread(self.__run, ())
        
    def __run(self):
        """
        请勿调用
        线程运行函数
        """
        while True:
            self.lock.acquire()
            try:
                self.func(*self.args, **self.kwargs)
            except Exception as e:
                print('Task_function_error:', e)
                pass
            if self.sec < 0 or not self.enable:
                self.stop_lock.release()
            else:
                time.sleep(self.sec)
                self.lock.release()

    def start(self):
        """运行线程"""
        self.lock.release()

    def stop(self):
        """暂停线程"""
        self.enable = False
        self.stop_lock.acquire()
        self.enable = True

    # def kill(self):
    #     pass
