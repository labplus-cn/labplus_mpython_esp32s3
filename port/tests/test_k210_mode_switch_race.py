"""Regression tests for camera mode switching and concurrent user callbacks."""

import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
import threading


PORT = Path(__file__).resolve().parents[1]
DRIVER = PORT / "file_system/common/smartcamera_new.py"
PUBLIC = PORT / "file_system/common/lib/k210_ai/public.py"


def load_camera():
    namespace = {
        "Pin": SimpleNamespace(P15=15, P16=16),
        "_thread": SimpleNamespace(allocate_lock=threading.Lock),
        "time": SimpleNamespace(sleep=lambda _: None, sleep_ms=lambda _: None),
        "gc": SimpleNamespace(collect=lambda: None),
    }
    for node in ast.parse(PUBLIC.read_text(encoding="utf-8")).body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and (node.targets[0].id.endswith("_MODE") or node.targets[0].id == "MODE")):
            namespace[node.targets[0].id] = ast.literal_eval(node.value)
    tree = ast.parse(DRIVER.read_text(encoding="utf-8"))
    camera_type = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "SmartCamera")
    helpers = [node for node in tree.body
               if (isinstance(node, ast.ClassDef) and node.name == "_ModeUART")
               or (isinstance(node, ast.FunctionDef) and node.name == "_control_operation")]
    exec(compile(ast.Module(body=helpers + [camera_type], type_ignores=[]), str(DRIVER), "exec"), namespace)
    camera = object.__new__(namespace["SmartCamera"])
    camera.lock = False
    camera._control_guard = threading.Lock()
    camera._uart_tx_guard = threading.Lock()
    camera._uart_generation = 0
    camera.connected = True
    camera.mode = namespace["FACE_DETECTION_MODE"]
    camera._model_parameters = {}
    camera._face_parameters = (1, 80)
    camera.slc_parameter = [3, 15, 11, 1]
    camera.face_detect = SimpleNamespace(lock=False, face_num=3)
    camera._request_mode_exit = lambda mode: None
    camera._clear_uart_input = lambda: None
    camera._wait_mode_init = lambda mode: None
    return camera, namespace


class ModeSwitchRaceTests(unittest.TestCase):
    def test_old_reference_remains_available_during_target_initialization(self):
        camera, namespace = load_camera()
        old = camera.face_detect
        def initialize(choice):
            # The main loop can read the attribute after the callback started.
            self.assertIs(camera.face_detect, old)
            self.assertTrue(camera.face_detect.lock)
            self.assertEqual(camera.face_detect.face_num, 3)
            camera.yolo_detect = SimpleNamespace(lock=False, id=None)
            camera.mode = namespace["OBJECT_RECOGNIZATION_MODE"]
        camera._yolo_detect_init = initialize
        self.assertTrue(camera.switcher_mode(namespace["OBJECT_RECOGNIZATION_MODE"], 1))
        self.assertIs(camera.face_detect, old)
        self.assertTrue(old.lock)

    def test_user_loop_can_read_during_all_three_mode_switches(self):
        camera, namespace = load_camera()
        state = {"mode": 0}
        sent = []

        class Model:
            def __init__(self, name):
                self.name = name
                self.lock = False
                self.face_num = 1
                self.id = 0
                self.category_list = ["object"]

            def recognize(self):
                if not self.lock:
                    sent.append(self.name)

        camera.face_detect = Model("face")
        camera.mode = namespace["FACE_DETECTION_MODE"]

        def user_loop():
            if state["mode"] == 0:
                camera.face_detect.recognize()
                if camera.face_detect.face_num != 0:
                    _ = camera.face_detect.face_num
            elif state["mode"] == 1:
                camera.yolo_detect.recognize()
                if camera.yolo_detect.id is not None:
                    _ = camera.yolo_detect.category_list[camera.yolo_detect.id]
            else:
                camera.guidepost.recognize()
                if camera.guidepost.id is not None:
                    _ = camera.guidepost.id

        def request_exit(mode):
            before = len(sent)
            user_loop()
            self.assertEqual(len(sent), before)

        def clear_input():
            user_loop()

        def make_initializer(attribute, mode):
            def initialize(choice):
                user_loop()
                setattr(camera, attribute, Model(attribute))
                camera.mode = mode
                user_loop()
            return initialize

        camera._request_mode_exit = request_exit
        camera._clear_uart_input = clear_input
        camera._yolo_detect_init = make_initializer("yolo_detect", namespace["OBJECT_RECOGNIZATION_MODE"])
        camera._guidepost_init = make_initializer("guidepost", namespace["GUIDEPOST_MODE"])
        camera._face_detect_init = make_initializer("face_detect", namespace["FACE_DETECTION_MODE"])

        for target, display_mode in ((3, 1), (10, 2), (4, 0)):
            self.assertTrue(camera.switcher_mode(target, 1))
            state["mode"] = display_mode
            user_loop()
        self.assertEqual(camera.mode, namespace["FACE_DETECTION_MODE"])
        self.assertTrue(camera.connected)

    def test_no_guidepost_detection_does_not_disable_new_requests(self):
        camera, namespace = load_camera()
        namespace["DEBUG"] = False
        camera.mode = namespace["GUIDEPOST_MODE"]
        camera.guidepost = SimpleNamespace(lock=False, id="stop", max_score=0.9,
                                           labels=["stop", "none", "left", "right"])
        camera._dispatch_uart_packet(bytes([0xbb, 0xaa, 1, 10, 2, 0xff, 0, 0, 0, 0, 0, 0]))
        self.assertFalse(camera.guidepost.lock)
        self.assertIsNone(camera.guidepost.id)
        camera._dispatch_uart_packet(bytes([0xbb, 0xaa, 1, 10, 2, 2, 80, 0, 0, 0, 0, 0]))
        self.assertFalse(camera.guidepost.lock)
        self.assertEqual(camera.guidepost.id, "left")

    def test_guidepost_requests_continue_after_empty_result(self):
        camera, namespace = load_camera()
        namespace["DEBUG"] = False
        ai_path = PORT / "file_system/common/lib/k210_ai/ai.py"
        tree = ast.parse(ai_path.read_text(encoding="utf-8"))
        cls = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "Guidepost")
        sent = []
        ai_namespace = {
            "time": SimpleNamespace(sleep_ms=lambda _: None),
            "AI": {"guidepost": [10, 1, 2]},
            "AI_Uart_CMD": lambda uart, data_type, cmd, cmd_type: sent.append((cmd, cmd_type)),
        }
        exec(compile(ast.Module(body=[cls], type_ignores=[]), str(ai_path), "exec"), ai_namespace)
        guide = object.__new__(ai_namespace["Guidepost"])
        guide.uart = object()
        guide.lock = False
        guide.id = None
        guide.max_score = 0
        guide.labels = ["stop", "none", "left", "right"]
        camera.mode = namespace["GUIDEPOST_MODE"]
        camera.guidepost = guide
        camera._dispatch_uart_packet(bytes([0xbb, 0xaa, 1, 10, 2, 0xff, 0, 0, 0, 0, 0, 0]))
        guide.recognize()
        self.assertEqual(sent, [(10, 2)])
        self.assertFalse(guide.lock)

    def test_failed_recovery_keeps_old_reference_locked(self):
        camera, namespace = load_camera()
        old = camera.face_detect
        def fail(mode):
            raise RuntimeError("lost acknowledgement")
        camera._request_mode_exit = fail
        camera._wait_for_ai_init_locked = lambda: False
        with self.assertRaises(RuntimeError):
            camera.switcher_mode(namespace["OBJECT_RECOGNIZATION_MODE"], 1)
        self.assertIs(camera.face_detect, old)
        self.assertTrue(old.lock)
        self.assertFalse(camera.connected)
        self.assertFalse(camera.lock)


if __name__ == "__main__":
    unittest.main()
