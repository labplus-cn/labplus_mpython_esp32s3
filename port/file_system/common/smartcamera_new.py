from machine import Pin, UART
from lib.k210_ai import *
import time
import gc
import _thread
from machine import Timer
gc.collect()

class SmartCamera:
    def __init__(self, rx=Pin.P15, tx=Pin.P16):
        self.sensor_choice = 1
        self.uart = UART(2, baudrate=1152000, rx=rx, tx=tx, rxbuf=MAX_BUF_SIZE)
        self.mode = DEFAULT_MODE
        self.lock = False
        self.connected = False
        self.last_error = None
        self._model_parameters = {}
        self._face_parameters = (1, 80)
        self._uart_rx = [bytearray()]
        self._uart_guard = _thread.allocate_lock()
        time.sleep(0.05)
        self.wait_for_ai_init()
        # self.thread_listen()
        self.tim9 = Timer(9)
        self.tim9.init(period=45, mode=Timer.PERIODIC, callback=self.timer9_tick)
        self.slc_parameter = [3, 15, 11, 1]
        self.a_status = 0
        self.b_status = 0
        self.tf_status = 0
        self.tf_sn = ''

    def _read_uart_packet(self, control=False):
        if self.lock and not control:
            return None
        if not self._uart_guard.acquire(False):
            return None
        try:
            if self.lock and not control:
                return None
            return uart_handle(self.uart, self._uart_rx)
        finally:
            self._uart_guard.release()

    def _clear_uart_input(self):
        self._uart_guard.acquire()
        try:
            self._uart_rx[0] = bytearray()
            pending = self.uart.any()
            if pending:
                self.uart.read(pending)
        finally:
            self._uart_guard.release()

    def wait_for_ai_init(self):
        previous_lock = self.lock
        self.lock = True
        try:
            self._invalidate_mode_objects()
            self._clear_uart_input()
            num = 0
            while True:
                gc.collect()
                time.sleep_ms(100)
                num += 1
                if num > 1000:
                    print('错误:通信超时，请检查接线情况及掌控拓展电源是否打开！')
                    print('Error: Communication timeout, please check the wiring and control whether the expansion power is turned on!')
                    self.last_error = "camera reset timed out"
                    return False
                AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFF)
                packet = self._read_uart_packet(control=True)
                while packet is not None:
                    if packet[2] == 0x01 and packet[3] == 0x01 and packet[4] == 0xFF:
                        print("AI摄像头重启中...")
                        print("AI camera is restarting")
                        time.sleep(0.5)
                        self._clear_uart_input()
                        break
                    packet = self._read_uart_packet(control=True)
                if packet is not None:
                    break

            time.sleep(1)
            num = 0
            while True:
                time.sleep_ms(100)
                gc.collect()
                num += 1
                if num > 1000:
                    print('错误:通信超时，请检查接线情况及掌控拓展电源是否打开！')
                    print('Error: Communication timeout, please check the wiring and control whether the expansion power is turned on!')
                    break
                if num % 30 == 0 and num < 500:
                    print('.' * int(num / 30))
                AI_Uart_CMD(self.uart, 0x01, 0x01, 0x00)
                time.sleep_ms(50)
                packet = self._read_uart_packet(control=True)
                while packet is not None:
                    if packet[2] == 0x01 and packet[3] == 0x01:
                        if packet[4] == 0x00:
                            AI_Uart_CMD(self.uart, 0x01, 0x01, 0x01)
                        elif packet[4] == 0x01:
                            print('==AI摄像头通信成功==')
                            print('==AI camera communication successful==')
                            time.sleep_ms(50)
                            self._clear_uart_input()
                            self.connected = True
                            self.last_error = None
                            return True
                    packet = self._read_uart_packet(control=True)
            self.last_error = "camera handshake timed out"
            return False
        except Exception as error:
            self.last_error = str(error)
            raise
        finally:
            self.lock = previous_lock

    def image_init(self):
        # 主绘图对象
        pass
    
    def sensor_config(self,_choice,_framesize,_pixformat,_w,_h,_vflip,_hmirror,_brightness,_contrast,_saturation,_gain,_whitebal,_freq,_dual_buff):
        self.lock=True
        _str = str(_framesize)+'|'+ str(_pixformat)+'|'+ str(_vflip)+'|'+ str(_hmirror)+'|'+ str(_brightness)+'|'+ str(_contrast)+'|'+ str(_saturation)+'|'+ str(_gain)+'|'+ str(_whitebal)+'|'+ str(_w)+'|'+ str(_h)+'|'+ str(_freq)+'|'+ str(_dual_buff)
        AI_Uart_CMD_String(uart=self.uart, cmd=AI['sensor'][0], cmd_type=AI['sensor'][1], cmd_data=[_choice,0,0], str_buf=_str)
        self.lock=False

    def mnist_init(self, _choice):
        self.mnist = MNIST(self.uart, choice=_choice)
        self.mode = MNIST_MODE

    def yolo_detect_init(self, _choice):
        self.yolo_detect = YOLO(self.uart, choice=_choice)
        self.mode = OBJECT_RECOGNIZATION_MODE

    def face_detect_init(self, _choice):
        self.face_detect = FACE_DETECT(self.uart, choice=_choice)
        self.mode = FACE_DETECTION_MODE

    def face_recognize_init(self, _face_num, _accuracy, _choice):
        self.fcr = Face_recogization(self.uart, face_num=_face_num, accuracy=_accuracy, choice=_choice)
        self.mode = FACE_RECOGNIZATION_MODE
        self._face_parameters = (_face_num, _accuracy)

    def asr_init(self):
        self.asr = Maix_asr(self.uart)
        self.mode = SPEECH_RECOGNIZATION_MODE
    
    def self_learning_classifier_init(self, _class_num, _sample_num, _threshold, _choice):
        self.slc = Self_learning_classfier(self.uart, class_num=_class_num, sample_num=_sample_num, choice=_choice)
        self.mode = SELF_LEARNING_CLASSIFIER_MODE
        self.slc_parameter = [_class_num, _sample_num, _threshold, _choice]

    def qrcode_init(self, _choice):
        self.qrcode = QRCode_recognization(self.uart, choice=_choice)
        self.mode = QRCODE_MODE

    def color_init(self, _choice):
        self.color = Color_recognization(self.uart, choice=_choice)
        self.mode = COLOE_MODE

    def guidepost_init(self, _choice):
        self.guidepost = Guidepost(self.uart, choice=_choice)
        self.mode = GUIDEPOST_MODE

    def kmodel_init(self, _choice, _komodel_path, width, height):
        self.kpu_model = KPU_MODEL(self.uart, choice=_choice, komodel_path=_komodel_path, width=width, height=height)
        self.mode = KPU_MODEL_MODE
        self._model_parameters[KPU_MODEL_MODE] = (_komodel_path, width, height)

    def track_init(self, _choice):
        self.track = Track(self.uart, choice=_choice)
        self.mode = TRACK_MODE
    
    def track_set_up(self,threshold,area_threshold):
        self.track.set_up(threshold=threshold,area_threshold=area_threshold)

    def color_statistics_init(self, _choice):
        self.color_statistics = Color_Statistics(self.uart, choice=_choice)
        self.mode = COLOR_STATISTICS_MODE

    def color_extracto_init(self, _choice):
        self.color_extracto = Color_Extracto(self.uart, choice=_choice)
        self.mode = COLOR_EXTRACTO_MODE

    def apriltag_init(self, _choice):
        self.apriltag = Apriltag(self.uart, choice=_choice)
        self.mode = APRILTAG_MODE

    def video_capture(self, choice=1, path="/sd/capture.avi", interval=100000, quality=50, width=320, height=240, duration=10):
        parameter = str(path)+'|'+ str(interval)+'|'+ str(width)+'|'+ str(height)+'|'+ str(duration)
        AI_Uart_CMD_String(uart=self.uart, cmd=AI['video'][0], cmd_type=AI['video'][1], cmd_data=[choice,quality,0], str_buf=parameter)

    def kmodel_yolo_init(self, _choice, _komodel_path, width, height, anchors):
        self.kpu_yolo_model = KPU_YOLO_MODEL(self.uart, choice=_choice, komodel_path=_komodel_path, width=width, height=height, anchors=anchors)
        self.mode = KPU_YOLO_MODEL_MODE
        self._model_parameters[KPU_YOLO_MODEL_MODE] = (_komodel_path, width, height, tuple(anchors))
        
    def canvas_init(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x01])
        self.mode = DEFAULT_MODE
        time.sleep_ms(50)
    
    def canvas_clear(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x02])
        time.sleep_ms(20)
    
    def canvas_txt(self,txt='',scale=1,x=0,y=0):
        _str = str([x,y,txt])
        AI_Uart_CMD_String(uart=self.uart, cmd=0x01, cmd_type=0xFA, cmd_data=[scale], str_buf=_str)
        time.sleep_ms(20)
    
    def rgb(self,r=0,g=0,b=0):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x03,int(r),int(g),int(b)])
        time.sleep_ms(20)
    
    def led(self,mode=0):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x04,int(mode)])
        time.sleep_ms(20)
    
    def factory_init(self):
        self.mode = FACTORY_MODE
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x01])
        time.sleep(1)
    
    def factory_lcd(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x02])
        time.sleep(1) 
    
    def factory_sensor(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x03])
        time.sleep(1)  
    
    def factory_rgb(self,r=0,g=0,b=0):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x04,int(r),int(g),int(b)])
        time.sleep(1)
    
    def factory_light(self,light=1):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x05])
        time.sleep(1)   
    
    def factory_write_sn(self,sn='sn'):
        _str = str([sn])
        AI_Uart_CMD_String(uart=self.uart, cmd=FACTORY_MODE, cmd_type=0x01, str_buf=_str)
        time.sleep(1)

    def thread_listen(self):
        self._task = TASK(func=self.uart_thread,sec=0.01)
        self._task.start()
    
    def timer9_tick(self,_):
        self.uart_thread()

    _MODE_INITIALIZERS = {
        DEFAULT_MODE: "canvas_init",
        MNIST_MODE: "mnist_init",
        OBJECT_RECOGNIZATION_MODE: "yolo_detect_init",
        FACE_DETECTION_MODE: "face_detect_init",
        FACE_RECOGNIZATION_MODE: "face_recognize_init",
        SELF_LEARNING_CLASSIFIER_MODE: "self_learning_classifier_init",
        COLOE_MODE: "color_init",
        QRCODE_MODE: "qrcode_init",
        GUIDEPOST_MODE: "guidepost_init",
        KPU_MODEL_MODE: "kmodel_init",
        TRACK_MODE: "track_init",
        COLOR_STATISTICS_MODE: "color_statistics_init",
        COLOR_EXTRACTO_MODE: "color_extracto_init",
        APRILTAG_MODE: "apriltag_init",
        KPU_YOLO_MODEL_MODE: "kmodel_yolo_init",
        FACTORY_MODE: "factory_init",
    }
    _MODE_OBJECTS = {
        MNIST_MODE: "mnist",
        OBJECT_RECOGNIZATION_MODE: "yolo_detect",
        FACE_DETECTION_MODE: "face_detect",
        FACE_RECOGNIZATION_MODE: "fcr",
        SELF_LEARNING_CLASSIFIER_MODE: "slc",
        COLOE_MODE: "color",
        QRCODE_MODE: "qrcode",
        SPEECH_RECOGNIZATION_MODE: "asr",
        GUIDEPOST_MODE: "guidepost",
        KPU_MODEL_MODE: "kpu_model",
        TRACK_MODE: "track",
        COLOR_STATISTICS_MODE: "color_statistics",
        COLOR_EXTRACTO_MODE: "color_extracto",
        APRILTAG_MODE: "apriltag",
        KPU_YOLO_MODEL_MODE: "kpu_yolo_model",
    }

    def _mode_name(self, mode):
        extra = {
            KPU_YOLO_MODEL_MODE: "KPU YOLO",
            VIDEO_MODE: "Video",
            FACTORY_MODE: "Factory",
        }
        if 0 <= mode < len(MODE):
            return MODE[mode]
        return extra.get(mode, str(mode))

    def _prepare_mode_init(self, mode, choice):
        if not isinstance(mode, int) or mode not in self._MODE_INITIALIZERS:
            raise ValueError("unsupported camera mode: " + str(mode))
        if not isinstance(choice, int) or not 0 <= choice <= 255:
            raise ValueError("camera choice must be a byte")
        method = self._MODE_INITIALIZERS[mode]
        if mode in (DEFAULT_MODE, FACTORY_MODE):
            args = ()
        elif mode == FACE_RECOGNIZATION_MODE:
            count, accuracy = self._face_parameters
            if (not isinstance(count, int) or not 1 <= count <= 255
                    or not isinstance(accuracy, int) or not 0 <= accuracy <= 100):
                raise ValueError("invalid face recognition parameters")
            args = (count, accuracy, choice)
        elif mode == SELF_LEARNING_CLASSIFIER_MODE:
            parameters = getattr(self, "slc_parameter", (3, 15, 11, 1))
            count, samples, threshold = parameters[:3]
            if (not isinstance(count, int) or not 1 <= count <= 255
                    or not isinstance(samples, int) or not 1 <= samples <= 255):
                raise ValueError("invalid self learning parameters")
            args = (count, samples, threshold, choice)
        elif mode in (KPU_MODEL_MODE, KPU_YOLO_MODEL_MODE):
            parameters = self._model_parameters.get(mode)
            if parameters is None:
                raise ValueError("initialize the custom model parameters before switching")
            path, width, height = parameters[:3]
            if (not isinstance(path, str) or not path or "|" in path
                    or not isinstance(width, int) or width <= 0
                    or not isinstance(height, int) or height <= 0):
                raise ValueError("invalid custom model parameters")
            payload = str(path) + "|" + str(width) + "|" + str(height)
            if mode == KPU_YOLO_MODEL_MODE:
                anchors = parameters[3]
                if (not anchors or len(anchors) % 2
                        or any(not isinstance(value, (int, float)) or value <= 0 for value in anchors)):
                    raise ValueError("invalid YOLO anchors")
                payload += "|" + str(anchors)
            if len(payload.encode("utf-8")) > 255:
                raise ValueError("custom model parameters exceed the UART payload limit")
            args = (choice,) + parameters
        else:
            args = (choice,)
        return method, args

    def _invalidate_mode_objects(self):
        for attribute in self._MODE_OBJECTS.values():
            model = getattr(self, attribute, None)
            if model is not None:
                model.lock = True
        self.mode = DEFAULT_MODE
        self.connected = False

    def _request_mode_exit(self, mode):
        for _ in range(1000):
            time.sleep_ms(50)
            gc.collect()
            AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFE, [mode])
            time.sleep_ms(50)
            packet = self._read_uart_packet(control=True)
            while packet is not None:
                if packet[2] == 0x01 and packet[3] == 0x01 and packet[4] == 0xFE:
                    return
                packet = self._read_uart_packet(control=True)
        raise RuntimeError("camera mode switch timed out")

    def switcher_mode(self, mode, choice):
        method, args = self._prepare_mode_init(mode, choice)
        if self.lock:
            raise RuntimeError("camera control is busy")
        attribute = self._MODE_OBJECTS.get(self.mode)
        old_model = getattr(self, attribute, None) if attribute else None
        if self.connected and self.mode == mode and (attribute is None or old_model is not None):
            print("当前模式:", self._mode_name(self.mode))
            return True

        self.lock = True
        self.last_error = None
        try:
            if old_model is not None:
                old_model.lock = True
            try:
                if not self.connected:
                    raise RuntimeError("camera connection needs recovery")
                self._request_mode_exit(mode)
                time.sleep(2)
                self._clear_uart_input()
                self._invalidate_mode_objects()
                getattr(self, method)(*args)
            except Exception as error:
                self.last_error = str(error)
                self._invalidate_mode_objects()
                if not self.wait_for_ai_init():
                    raise RuntimeError("camera recovery failed: " + self.last_error)
                getattr(self, method)(*args)
            self.connected = True
            self.last_error = None
            time.sleep(1)
            print("当前模式:", self._mode_name(self.mode))
            return True
        except Exception as error:
            self.last_error = str(error)
            self._invalidate_mode_objects()
            raise
        finally:
            self.lock = False

    def uart_thread(self):
        gc.collect()
        for _ in range(32):
            if self.lock or not self.connected:
                return
            packet = self._read_uart_packet()
            if packet is None:
                return
            self._dispatch_uart_packet(packet)

    def _dispatch_uart_packet(self, CMD):
        if DEBUG:
            print(CMD)
            print("===CMD===")          
        if(self.lock==False):
            if(CMD==None):
                return

            if(self.mode==DEFAULT_MODE):
                pass
            elif(self.mode==MNIST_MODE and self.mnist!=None):
                if(len(CMD)>0):
                    if(CMD[3]==MNIST_MODE and CMD[4]==0x02):
                        if(CMD[5]==0xff):
                            self.mnist.lock = True
                            self.mnist.id,self.mnist.max_score = None,0
                        else:
                            self.mnist.id,self.mnist.max_score = CMD[5],round(int(CMD[6])/100,2)
                    elif(CMD[2]==0x02):
                        pass
                else:
                    pass
            elif(self.mode==OBJECT_RECOGNIZATION_MODE and self.yolo_detect!=None):
                if(len(CMD)>0):
                    if(CMD[3]==OBJECT_RECOGNIZATION_MODE and CMD[4]==0x02):
                        if(CMD[5]==0xff):
                            self.yolo_detect.lock = True
                            self.yolo_detect.id,self.yolo_detect.max_score,self.yolo_detect.objnum = None,0,0
                        else:
                            self.yolo_detect.id,self.yolo_detect.max_score,self.yolo_detect.objnum = CMD[5],round(int(CMD[6])/100,2),CMD[7]
                    elif(CMD[2]==0x02):
                        pass
                else:
                    pass
            elif(self.mode==FACE_DETECTION_MODE and self.face_detect!=None):
                if(len(CMD)>0):
                    if(CMD[3]==FACE_DETECTION_MODE and CMD[4]==0x02):
                        if(CMD[5]==0xff):
                            self.face_detect.lock = True
                            self.face_detect.face_num,self.face_detect.max_score = None,0
                        else:
                            self.face_detect.face_num,self.face_detect.max_score = CMD[5],round(int(CMD[6])/100,2)
                    elif(CMD[2]==0x02):
                        pass
                else:
                    pass
            elif(self.mode==FACE_RECOGNIZATION_MODE and self.fcr!=None):#5
                if(len(CMD)>0):
                    if(CMD[3]==FACE_RECOGNIZATION_MODE and CMD[4]==0x03):
                        if(CMD[5]==0xff):
                            self.fcr.lock = True
                            self.fcr.id,self.fcr.max_score = None,0
                        else:
                            self.fcr.id,self.fcr.max_score = CMD[5],round(int(CMD[6])/100,2)
                    elif(CMD[2]==0x02):
                        pass
                else:
                    pass
            elif(self.mode==SELF_LEARNING_CLASSIFIER_MODE and self.slc!=None):#6
                if(len(CMD)>0):
                    if(CMD[3]==SELF_LEARNING_CLASSIFIER_MODE and CMD[4]==0x03):
                        if(CMD[5]==0xff):
                            self.slc.lock = True
                            self.slc.id,self.slc.max_score = None,None
                        else:
                            self.slc.id,self.slc.max_score = CMD[5],CMD[6]/10
                    elif(CMD[2]==0x02):
                        pass
                else:
                    pass
            elif(self.mode==COLOE_MODE and self.color!=None):#7
                try:
                    if(len(CMD)>0):
                        if(CMD[3]==COLOE_MODE and CMD[4]==0x03):
                            if(CMD[5]==0xff):
                                self.color.lock = True
                                self.color.id = None
                            else:
                                self.color.id = int(CMD[5])
                    else:
                        self.color.id = None
                except:
                    self.color.id = None
            elif(self.mode==QRCODE_MODE and self.qrcode!=None):
                try:
                    if(len(CMD)>0):
                        if(CMD[2]==0x01 and CMD[3]==QRCODE_MODE and CMD[4]==0x03 and CMD[5]==0xff):#
                            self.qrcode.lock = True
                            self.qrcode.id = None
                        elif(CMD[2]==0x02 and CMD[3]==QRCODE_MODE and CMD[4]==0x03):
                            self.qrcode.lock = True
                            _str = str(bytes(CMD[21:-1]).decode('UTF-8','ignore'))
                            data = eval(_str)
                            self.qrcode.id = data[0]
                            self.qrcode.info = data[1]
                    else:
                        self.qrcode.id = None
                        self.qrcode.info = None
                except Exception as e:
                    print("QRCODE ERROR:",e)
                    self.qrcode.id = None
                    self.qrcode.info = None
            elif(self.mode==SPEECH_RECOGNIZATION_MODE and self.asr!=None):
                try:
                    if(len(CMD)>0):
                        if(CMD[3]==SPEECH_RECOGNIZATION_MODE and CMD[4]==0x03):
                            if(CMD[5]==0xff):
                                self.asr.lock = True
                                self.asr.result = None
                            else:
                                self.asr.result = int(CMD[5])
                        elif(CMD[2]==0x02):
                            self.asr.result = None
                    else:
                        self.asr.result = None
                except:
                    self.asr.result = None
            elif(self.mode==GUIDEPOST_MODE and self.guidepost!=None):#10
                if(len(CMD)>0):
                    if(CMD[3]==GUIDEPOST_MODE and CMD[4]==0x02):
                        if(CMD[5]==0xff):
                            self.guidepost.lock = True
                            self.guidepost.id,self.guidepost.max_score = None,0
                        else:
                            max_index,self.guidepost.max_score = CMD[5],round(int(CMD[6])/100,2)
                            self.guidepost.id = self.guidepost.labels[max_index]
                    elif(CMD[2]==0x02):
                        pass
                else:
                    self.guidepost.id,self.guidepost.max_score = None,0
            elif(self.mode==KPU_MODEL_MODE and self.kpu_model!=None):
                if(len(CMD)>0):
                    if(CMD[3]==KPU_MODEL_MODE and CMD[4]==0x03):
                        if(CMD[5]==0xff):
                            self.kpu_model.lock = True
                            self.kpu_model.id,self.kpu_model.max_score = None,0
                        else:
                            self.kpu_model.id ,self.kpu_model.max_score = CMD[5],round(int(CMD[6])/100,2)
                    elif(CMD[2]==0x02):
                        pass
                else:
                    self.kpu_model.id ,self.kpu_model.max_score = None,0
            elif(self.mode==TRACK_MODE and self.track!=None):
                if(len(CMD)>0):
                    if(CMD[2]==0x01 and CMD[3]==TRACK_MODE and CMD[4]==0x02 and CMD[5]==0xff):
                        self.track.lock = True
                        self.track.x,self.track.y,self.track.cx,self.track.cy,self.track.w,self.track.h,self.track.pixels,self.track.count,self.track.code = None,None,None,None,None,None,None,None,None
                    elif(CMD[2]==0x02 and CMD[3]==TRACK_MODE and CMD[4]==0x02):
                        self.track.lock = True
                        _str = str(bytes(CMD[21:-1]).decode('UTF-8','ignore'))
                        data = _str.split('|')
                        self.track.x,self.track.y,self.track.cx,self.track.cy,self.track.w,self.track.h,self.track.pixels,self.track.count,self.track.code = int(data[0]),int(data[1]),int(data[2]),int(data[3]),int(data[4]),int(data[5]),int(data[6]),int(data[7]),hammingWeight(int(CMD[5]))        
                    else:
                        self.track.x,self.track.y,self.track.cx,self.track.cy,self.track.w,self.track.h,self.track.pixels,self.track.count,self.track.code = None,None,None,None,None,None,None,None,None          
                else:
                    self.track.x,self.track.y,self.track.cx,self.track.cy,self.track.w,self.track.h,self.track.pixels,self.track.count,self.track.code = None,None,None,None,None,None,None,None,None
            elif(self.mode==COLOR_STATISTICS_MODE and self.color_statistics!=None):
                if(len(CMD)>0):
                    if(CMD[2]==0x01 and CMD[3]==AI['color_statistics'][0] and CMD[4]==AI['color_statistics'][2] and CMD[5]==0xff):
                        self.color_statistics.lock = True
                        self.color_statistics.row_data1,self.color_statistics.row_data2,self.color_statistics.row_data3,self.color_statistics.line_get_regression_data = [None,None,None,None,None],[None,None,None,None,None],[None,None,None,None,None],[None,None,None,None,None,None,None,None]
                    elif(CMD[2]==0x02 and CMD[3]==AI['color_statistics'][0] and CMD[4]==AI['color_statistics'][2]):
                        self.color_statistics.lock = True
                        _str = str(bytes(CMD[21:-1]).decode('UTF-8','ignore'))
                        data = eval(_str)
                        self.color_statistics.row_data1,self.color_statistics.row_data2,self.color_statistics.row_data3,self.color_statistics.line_get_regression_data = [int(CMD[5]),int(CMD[6]),int(CMD[7]),int(CMD[8]),int(CMD[9])],[int(CMD[10]),int(CMD[11]),int(CMD[12]),int(CMD[13]),int(CMD[14])],[int(CMD[15]),int(CMD[16]),int(CMD[17]),int(CMD[18]),int(CMD[19])],[int(data[0]),int(data[1]),int(data[2]),int(data[3]),int(data[4]),int(data[5]),int(data[6]),int(data[7])]
                    self.color_statistics.data = [self.color_statistics.row_data1,self.color_statistics.row_data2,self.color_statistics.row_data3]
                # else:
                #     self.color_statistics.row_data1,self.color_statistics.row_data2,self.color_statistics.row_data3,self.color_statistics.line_get_regression_data = [None,None,None,None,None],[None,None,None,None,None],[None,None,None,None,None],[None,None,None,None,None,None,None,None]
                #     self.color_statistics.data = [self.color_statistics.row_data1,self.color_statistics.row_data2,self.color_statistics.row_data3]
            elif(self.mode==COLOR_EXTRACTO_MODE and self.color_extracto!=None):
                if(len(CMD)>0):
                    if(CMD[2]==0x01 and CMD[3]==AI['color_extracto'][0] and CMD[4]==AI['color_extracto'][2] and CMD[5]==0xff):
                        self.color_extracto.lock = True
                        self.color_extracto.L,self.color_extracto.A,self.color_extracto.B = None,None,None
                    elif(CMD[2]==0x02 and CMD[3]==AI['color_extracto'][0] and CMD[4]==AI['color_extracto'][2]):
                        _str = str(bytes(CMD[21:-1]).decode('UTF-8','ignore'))
                        data = _str.split('|')
                        self.color_extracto.L,self.color_extracto.A,self.color_extracto.B = int(data[0]),int(data[1]),int(data[2])
                    self.color_extracto.LAB_Data = [self.color_extracto.L,self.color_extracto.A,self.color_extracto.B]
                else:
                    self.color_extracto.L,self.color_extracto.A,self.color_extracto.B = None,None,None
                    self.color_extracto.LAB_Data = [self.color_extracto.L,self.color_extracto.A,self.color_extracto.B]
            elif(self.mode==APRILTAG_MODE and self.apriltag!=None):
                # print(CMD)
                if(len(CMD)>0):
                    if(CMD[2]==0x01 and CMD[3]==AI['apriltag'][0] and CMD[4]==AI['apriltag'][2] and CMD[5]==0xff):
                        self.apriltag.lock = True
                        self.apriltag.tag_family,self.apriltag.tag_id = None,None
                    elif(CMD[2]==0x02 and CMD[3]==AI['apriltag'][0] and CMD[4]==AI['apriltag'][2]):
                        self.apriltag.lock = True
                        # _str = str(bytes(CMD[21:-1]).decode('UTF-8','ignore'))
                        _str = str(bytes(CMD[21:-1]).decode('UTF-8','ignore'))
                        data = _str.split('|')
                        self.apriltag.tag_family,self.apriltag.tag_id = int(data[0]),int(data[1])
                else:
                    self.apriltag.tag_family,self.apriltag.tag_id = None,None
            elif(self.mode==KPU_YOLO_MODEL_MODE and self.kpu_yolo_model!=None):
                if(len(CMD)>0):
                    if(CMD[3]==KPU_YOLO_MODEL_MODE and CMD[4]==0x03):
                        if(CMD[5]==0xff):
                            self.kpu_yolo_model.lock = True
                            self.kpu_yolo_model.id,self.kpu_yolo_model.max_score = None,0
                        else:
                            self.kpu_yolo_model.id ,self.kpu_yolo_model.max_score = CMD[5],round(int(CMD[6])/100,2)
                    elif(CMD[2]==0x02):
                        pass
                else:
                    self.kpu_yolo_model.id ,self.kpu_yolo_model.max_score = None,0
            elif(self.mode==FACTORY_MODE):
                if(len(CMD)>0):
                    # if(CMD[2]==0x01 and CMD[3]==FACTORY_MODE and CMD[4]==0x01):
                    #     self.a_status,self.b_status,self.tf_status = CMD[5],CMD[6],CMD[7]
                    if(CMD[2]==0x02 and CMD[3]==FACTORY_MODE and CMD[4]==0x01):
                        _str = str(bytes(CMD[21:-1]).decode('UTF-8','ignore'))
                        data = eval(_str)
                        self.a_status,self.b_status,self.tf_status = CMD[5],CMD[6],CMD[7]
                        self.tf_sn = data[0]
                else:
                    pass
            else:
                pass
            
