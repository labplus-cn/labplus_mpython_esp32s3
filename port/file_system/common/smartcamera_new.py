from machine import Pin, UART
from lib.k210_ai import *
import time
import gc
import _thread
from machine import Timer
gc.collect()

class _CameraUART:
    """Serialize every write to the physical camera UART."""
    def __init__(self, uart, guard):
        self._uart = uart
        self._guard = guard

    def write(self, data):
        self._guard.acquire()
        try:
            return self._uart.write(data)
        finally:
            self._guard.release()

    def any(self):
        return self._uart.any()

    def read(self, size=None):
        if size is None:
            return self._uart.read()
        return self._uart.read(size)

    def __getattr__(self, name):
        return getattr(self._uart, name)


class _ModeUART:
    """UART handle invalidated when its camera mode is switched away."""
    def __init__(self, camera, generation):
        self.camera = camera
        self.generation = generation

    def write(self, data):
        camera = self.camera
        guard = camera._uart_tx_guard
        guard.acquire()
        try:
            if self.generation != camera._uart_generation:
                return 0
            return camera._uart_device.write(data)
        finally:
            guard.release()


def _control_operation(method):
    def guarded(self, *args, **kwargs):
        if not self._control_guard.acquire(False):
            raise RuntimeError("camera control is busy")
        if self.lock:
            self._control_guard.release()
            raise RuntimeError("camera control is busy")
        self.lock = True
        model = None
        previous_model_lock = None
        try:
            attribute = getattr(self, "_MODE_OBJECTS", {}).get(self.mode)
            model = getattr(self, attribute, None) if attribute else None
            previous_model_lock = getattr(model, "lock", None) if model is not None else None
            if model is not None:
                model.lock = True
            return method(self, *args, **kwargs)
        finally:
            try:
                if model is not None:
                    model.lock = previous_model_lock
            finally:
                self.lock = False
                self._control_guard.release()
    return guarded


class SmartCamera:
    def __init__(self, rx=Pin.P15, tx=Pin.P16):
        self.sensor_choice = 1
        self._uart_tx_guard = _thread.allocate_lock()
        self._uart_device = UART(2, baudrate=1152000, rx=rx, tx=tx, rxbuf=MAX_BUF_SIZE)
        self.uart = _CameraUART(self._uart_device, self._uart_tx_guard)
        self.mode = DEFAULT_MODE
        self.lock = False
        self.connected = False
        self.last_error = None
        self._model_parameters = {}
        self._face_parameters = (1, 80)
        self._uart_rx = [bytearray()]
        self._uart_guard = _thread.allocate_lock()
        self._control_guard = _thread.allocate_lock()
        self._uart_generation = 0
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

    def _mode_uart(self):
        return _ModeUART(self, getattr(self, "_uart_generation", 0))

    def _store_mode_object(self, attribute, model):
        model.lock = True
        setattr(self, attribute, model)

    def _invalidate_mode_uart(self):
        guard = self._uart_tx_guard
        guard.acquire()
        try:
            self._uart_generation += 1
        finally:
            guard.release()

    def wait_for_ai_init(self):
        if not self._control_guard.acquire(False):
            raise RuntimeError("camera control is busy")
        if self.lock:
            self._control_guard.release()
            raise RuntimeError("camera control is busy")
        try:
            return self._wait_for_ai_init_locked()
        finally:
            self._control_guard.release()

    def _wait_for_ai_init_locked(self):
        previous_lock = self.lock
        self.lock = True
        try:
            self._invalidate_mode_uart()
            self._invalidate_mode_objects()
            self._clear_uart_input()
            num = 0
            while True:
                gc.collect()
                time.sleep_ms(100)
                num += 1
                if num > 100:
                    print('错误:通信超时，请检查接线情况及掌控拓展电源是否打开！')
                    print('Error: Communication timeout, please check the wiring and control whether the expansion power is turned on!')
                    self.last_error = "camera reset timed out"
                    self.connected = False
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
                if num > 200:
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
            self.connected = False
            return False
        except Exception as error:
            self.last_error = str(error)
            raise
        finally:
            self.lock = previous_lock

    def image_init(self):
        # 主绘图对象
        pass
    
    @_control_operation
    def sensor_config(self,_choice,_framesize,_pixformat,_w,_h,_vflip,_hmirror,_brightness,_contrast,_saturation,_gain,_whitebal,_freq,_dual_buff):
        _str = str(_framesize)+'|'+ str(_pixformat)+'|'+ str(_vflip)+'|'+ str(_hmirror)+'|'+ str(_brightness)+'|'+ str(_contrast)+'|'+ str(_saturation)+'|'+ str(_gain)+'|'+ str(_whitebal)+'|'+ str(_w)+'|'+ str(_h)+'|'+ str(_freq)+'|'+ str(_dual_buff)
        AI_Uart_CMD_String(uart=self.uart, cmd=AI['sensor'][0], cmd_type=AI['sensor'][1], cmd_data=[_choice,0,0], str_buf=_str)

    def _mnist_init(self, _choice):
        self._store_mode_object("mnist", MNIST(self._mode_uart(), choice=_choice))
        self.mode = MNIST_MODE

    def _yolo_detect_init(self, _choice):
        self._store_mode_object("yolo_detect", YOLO(self._mode_uart(), choice=_choice))
        self.mode = OBJECT_RECOGNIZATION_MODE

    def _face_detect_init(self, _choice):
        self._store_mode_object("face_detect", FACE_DETECT(self._mode_uart(), choice=_choice))
        self.mode = FACE_DETECTION_MODE

    def _face_recognize_init(self, _face_num, _accuracy, _choice):
        self._store_mode_object("fcr", Face_recogization(self._mode_uart(), face_num=_face_num, accuracy=_accuracy, choice=_choice))
        self.mode = FACE_RECOGNIZATION_MODE
        self._face_parameters = (_face_num, _accuracy)

    def asr_init(self):
        raise ValueError("speech recognition is not supported by the current K210 firmware")
    
    def _self_learning_classifier_init(self, _class_num, _sample_num, _threshold, _choice):
        self._store_mode_object("slc", Self_learning_classfier(self._mode_uart(), class_num=_class_num, sample_num=_sample_num, choice=_choice))
        self.mode = SELF_LEARNING_CLASSIFIER_MODE
        self.slc_parameter = [_class_num, _sample_num, _threshold, _choice]

    def _qrcode_init(self, _choice):
        self._store_mode_object("qrcode", QRCode_recognization(self._mode_uart(), choice=_choice))
        self.mode = QRCODE_MODE

    def _color_init(self, _choice):
        self._store_mode_object("color", Color_recognization(self._mode_uart(), choice=_choice))
        self.mode = COLOE_MODE

    def _guidepost_init(self, _choice):
        self._store_mode_object("guidepost", Guidepost(self._mode_uart(), choice=_choice))
        self.mode = GUIDEPOST_MODE

    def _kmodel_init(self, _choice, _komodel_path, width, height):
        self._store_mode_object("kpu_model", KPU_MODEL(self._mode_uart(), choice=_choice, komodel_path=_komodel_path, width=width, height=height))
        self.mode = KPU_MODEL_MODE
        self._model_parameters[KPU_MODEL_MODE] = (_komodel_path, width, height)

    def _track_init(self, _choice):
        self._store_mode_object("track", Track(self._mode_uart(), choice=_choice))
        self.mode = TRACK_MODE
    
    @_control_operation
    def track_set_up(self,threshold,area_threshold):
        self.track.set_up(threshold=threshold,area_threshold=area_threshold)

    def _color_statistics_init(self, _choice):
        self._store_mode_object("color_statistics", Color_Statistics(self._mode_uart(), choice=_choice))
        self.mode = COLOR_STATISTICS_MODE

    def _color_extracto_init(self, _choice):
        self._store_mode_object("color_extracto", Color_Extracto(self._mode_uart(), choice=_choice))
        self.mode = COLOR_EXTRACTO_MODE

    def _apriltag_init(self, _choice):
        self._store_mode_object("apriltag", Apriltag(self._mode_uart(), choice=_choice))
        self.mode = APRILTAG_MODE

    @_control_operation
    def video_capture(self, choice=1, path="/sd/capture.avi", interval=100000, quality=50, width=320, height=240, duration=10):
        parameter = str(path)+'|'+ str(interval)+'|'+ str(width)+'|'+ str(height)+'|'+ str(duration)
        AI_Uart_CMD_String(uart=self.uart, cmd=AI['video'][0], cmd_type=AI['video'][1], cmd_data=[choice,quality,0], str_buf=parameter)

    def _kmodel_yolo_init(self, _choice, _komodel_path, width, height, anchors):
        self._store_mode_object("kpu_yolo_model", KPU_YOLO_MODEL(self._mode_uart(), choice=_choice, komodel_path=_komodel_path, width=width, height=height, anchors=anchors))
        self.mode = KPU_YOLO_MODEL_MODE
        self._model_parameters[KPU_YOLO_MODEL_MODE] = (_komodel_path, width, height, tuple(anchors))
        
    def _canvas_init(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x01])
        self.mode = DEFAULT_MODE
        time.sleep_ms(50)
    
    @_control_operation
    def canvas_clear(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x02])
        time.sleep_ms(20)
    
    @_control_operation
    def canvas_txt(self,txt='',scale=1,x=0,y=0):
        _str = str([x,y,txt])
        AI_Uart_CMD_String(uart=self.uart, cmd=0x01, cmd_type=0xFA, cmd_data=[scale], str_buf=_str)
        time.sleep_ms(20)
    
    @_control_operation
    def rgb(self,r=0,g=0,b=0):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x03,int(r),int(g),int(b)])
        time.sleep_ms(20)
    
    @_control_operation
    def led(self,mode=0):
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFA, [0x04,int(mode)])
        time.sleep_ms(20)
    
    def _factory_init(self):
        self.mode = FACTORY_MODE
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x01])
        time.sleep(1)

    # All public initializers use the same exit, initialization and ACK flow.
    def mnist_init(self, _choice):
        return self._switch_to_initializer(MNIST_MODE, '_mnist_init', (_choice,))

    def yolo_detect_init(self, _choice):
        return self._switch_to_initializer(OBJECT_RECOGNIZATION_MODE, '_yolo_detect_init', (_choice,))

    def face_detect_init(self, _choice):
        return self._switch_to_initializer(FACE_DETECTION_MODE, '_face_detect_init', (_choice,))

    def face_recognize_init(self, _face_num, _accuracy, _choice):
        return self._switch_to_initializer(FACE_RECOGNIZATION_MODE, '_face_recognize_init', (_face_num, _accuracy, _choice))

    def self_learning_classifier_init(self, _class_num, _sample_num, _threshold, _choice):
        return self._switch_to_initializer(SELF_LEARNING_CLASSIFIER_MODE, '_self_learning_classifier_init', (_class_num, _sample_num, _threshold, _choice))

    def qrcode_init(self, _choice):
        return self._switch_to_initializer(QRCODE_MODE, '_qrcode_init', (_choice,))

    def color_init(self, _choice):
        return self._switch_to_initializer(COLOE_MODE, '_color_init', (_choice,))

    def guidepost_init(self, _choice):
        return self._switch_to_initializer(GUIDEPOST_MODE, '_guidepost_init', (_choice,))

    def kmodel_init(self, _choice, _komodel_path, width, height):
        return self._switch_to_initializer(KPU_MODEL_MODE, '_kmodel_init', (_choice, _komodel_path, width, height))

    def track_init(self, _choice):
        return self._switch_to_initializer(TRACK_MODE, '_track_init', (_choice,))

    def color_statistics_init(self, _choice):
        return self._switch_to_initializer(COLOR_STATISTICS_MODE, '_color_statistics_init', (_choice,))

    def color_extracto_init(self, _choice):
        return self._switch_to_initializer(COLOR_EXTRACTO_MODE, '_color_extracto_init', (_choice,))

    def apriltag_init(self, _choice):
        return self._switch_to_initializer(APRILTAG_MODE, '_apriltag_init', (_choice,))

    def kmodel_yolo_init(self, _choice, _komodel_path, width, height, anchors):
        return self._switch_to_initializer(KPU_YOLO_MODEL_MODE, '_kmodel_yolo_init', (_choice, _komodel_path, width, height, anchors))

    def canvas_init(self):
        return self._switch_to_initializer(DEFAULT_MODE, '_canvas_init', ())

    def factory_init(self):
        return self._switch_to_initializer(FACTORY_MODE, '_factory_init', ())
    
    @_control_operation
    def factory_lcd(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x02])
        time.sleep(1) 
    
    @_control_operation
    def factory_sensor(self):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x03])
        time.sleep(1)  
    
    @_control_operation
    def factory_rgb(self,r=0,g=0,b=0):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x04,int(r),int(g),int(b)])
        time.sleep(1)
    
    @_control_operation
    def factory_light(self,light=1):
        AI_Uart_CMD(self.uart, 0x01, 0x01, FACTORY_MODE, [0x05])
        time.sleep(1)   
    
    @_control_operation
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
        DEFAULT_MODE: "_canvas_init",
        MNIST_MODE: "_mnist_init",
        OBJECT_RECOGNIZATION_MODE: "_yolo_detect_init",
        FACE_DETECTION_MODE: "_face_detect_init",
        FACE_RECOGNIZATION_MODE: "_face_recognize_init",
        SELF_LEARNING_CLASSIFIER_MODE: "_self_learning_classifier_init",
        COLOE_MODE: "_color_init",
        QRCODE_MODE: "_qrcode_init",
        GUIDEPOST_MODE: "_guidepost_init",
        KPU_MODEL_MODE: "_kmodel_init",
        TRACK_MODE: "_track_init",
        COLOR_STATISTICS_MODE: "_color_statistics_init",
        COLOR_EXTRACTO_MODE: "_color_extracto_init",
        APRILTAG_MODE: "_apriltag_init",
        KPU_YOLO_MODEL_MODE: "_kmodel_yolo_init",
        FACTORY_MODE: "_factory_init",
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
        for _ in range(100):
            time.sleep_ms(50)
            gc.collect()
            AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFE, [mode])
            time.sleep_ms(50)
            packet = self._read_uart_packet(control=True)
            while packet is not None:
                remote_error = self._device_error_text(packet, mode, 0)
                if remote_error is not None:
                    raise RuntimeError("camera mode exit failed: " + remote_error)
                if packet[2] == 0x01 and packet[3] == 0x01 and packet[4] == 0xFE:
                    return
                packet = self._read_uart_packet(control=True)
        raise RuntimeError("camera mode switch timed out")

    def _device_error_text(self, packet, mode, nonce):
        if (packet is None or len(packet) < 22 or packet[2] != 0x02
                or packet[3] != 0x01 or packet[4] != 0xFB
                or packet[5] != mode or packet[6] != nonce):
            return None
        message = bytes(packet[21:-1]).decode("utf-8", "ignore")
        print("[K210 error]", message)
        return message or "unspecified device error"

    def switcher_mode(self, mode, choice):
        method, args = self._prepare_mode_init(mode, choice)
        return self._switch_to_initializer(mode, method, args)

    def _wait_mode_init(self, mode):
        # A missing reply is an error, including when running old camera firmware.
        nonce = (getattr(self, '_mode_query_nonce', 0) % 255) + 1
        self._mode_query_nonce = nonce
        remote_error = None
        # UART ordering ensures this query is processed after the init command.
        AI_Uart_CMD(self.uart, 0x01, 0x01, 0xFC, [mode, nonce])
        for _ in range(600):
            packet = self._read_uart_packet(control=True)
            while packet is not None:
                detail = self._device_error_text(packet, mode, nonce)
                if detail is not None:
                    remote_error = detail
                if (packet[2] == 0x01 and packet[3] == 0x01
                        and packet[4] == 0xFD and packet[5] == mode
                        and packet[7] == nonce):
                    if packet[6] == 1:
                        return
                    message = 'camera mode initialization failed: ' + str(mode)
                    if remote_error is not None:
                        message += ': ' + remote_error
                    raise RuntimeError(message)
                packet = self._read_uart_packet(control=True)
            time.sleep_ms(50)
        raise RuntimeError('camera mode initialization timed out; update camera firmware')

    def _switch_to_initializer(self, mode, method, args):
        if not self._control_guard.acquire(False):
            raise RuntimeError("camera control is busy")
        if self.lock:
            self._control_guard.release()
            raise RuntimeError("camera control is busy")
        self.lock = True
        self.last_error = None
        try:
            self._invalidate_mode_uart()
            attribute = self._MODE_OBJECTS.get(self.mode)
            old_model = getattr(self, attribute, None) if attribute else None
            if old_model is not None:
                old_model.lock = True
            try:
                if not self.connected:
                    raise RuntimeError("camera connection needs recovery")
                self._request_mode_exit(mode)
                time.sleep(2)
            except Exception as error:
                self.last_error = str(error)
                self._invalidate_mode_objects()
                if not self._wait_for_ai_init_locked():
                    raise RuntimeError("camera recovery failed: " + self.last_error)
            self._clear_uart_input()
            self._invalidate_mode_objects()
            getattr(self, method)(*args)
            target_attribute = self._MODE_OBJECTS.get(mode)
            target_model = getattr(self, target_attribute, None) if target_attribute else None
            if target_model is not None:
                target_model.lock = True
            self._wait_mode_init(mode)
            if target_model is not None:
                target_model.lock = False
            self.connected = True
            self.last_error = None
            time.sleep(1)
            print("当前模式:", self._mode_name(self.mode))
            return True
        except Exception as error:
            self.last_error = str(error)
            self._invalidate_mode_uart()
            self._invalidate_mode_objects()
            raise
        finally:
            self.lock = False
            self._control_guard.release()

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
        if (CMD is not None and len(CMD) >= 22 and CMD[2] == 0x02
                and CMD[3] == 0x01 and CMD[4] == 0xFB):
            message = bytes(CMD[21:-1]).decode("utf-8", "ignore")
            print("[K210 error mode %d]" % CMD[5], message)
            self.last_error = message or "unspecified device error"
            return
        if(self.lock==False):
            if(CMD==None):
                return

            if(self.mode==DEFAULT_MODE):
                pass
            elif(self.mode==MNIST_MODE and self.mnist!=None):
                if(len(CMD)>0):
                    if(CMD[3]==MNIST_MODE and CMD[4]==0x02):
                        if(CMD[5]==0xff):
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
                            self.face_detect.face_num,self.face_detect.max_score = 0,0
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
                            self.qrcode.id = None
                            self.qrcode.info = None
                        elif(CMD[2]==0x02 and CMD[3]==QRCODE_MODE and CMD[4]==0x03):
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
                        self.track.x,self.track.y,self.track.cx,self.track.cy,self.track.w,self.track.h,self.track.pixels,self.track.count,self.track.code = None,None,None,None,None,None,None,None,None
                    elif(CMD[2]==0x02 and CMD[3]==TRACK_MODE and CMD[4]==0x02):
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
                        self.color_statistics.row_data1,self.color_statistics.row_data2,self.color_statistics.row_data3,self.color_statistics.line_get_regression_data = [None,None,None,None,None],[None,None,None,None,None],[None,None,None,None,None],[None,None,None,None,None,None,None,None]
                    elif(CMD[2]==0x02 and CMD[3]==AI['color_statistics'][0] and CMD[4]==AI['color_statistics'][2]):
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
                        self.apriltag.tag_family,self.apriltag.tag_id = None,None
                    elif(CMD[2]==0x02 and CMD[3]==AI['apriltag'][0] and CMD[4]==AI['apriltag'][2]):
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
            
