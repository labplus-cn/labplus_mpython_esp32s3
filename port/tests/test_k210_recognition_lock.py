"""Regression tests for recognition after an empty or successful reply."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
from test_k210_mode_switch_race import load_camera, PORT, PUBLIC

AI_PATH = PORT / 'file_system/common/lib/k210_ai/ai.py'


def load_ai_classes():
    public = ast.parse(PUBLIC.read_text(encoding='utf-8'))
    ai = next(ast.literal_eval(n.value) for n in public.body
              if isinstance(n, ast.Assign) and len(n.targets) == 1
              and isinstance(n.targets[0], ast.Name) and n.targets[0].id == 'AI')
    sent = []
    namespace = {'AI': ai, 'time': SimpleNamespace(sleep=lambda _: None, sleep_ms=lambda _: None),
                 'AI_Uart_CMD': lambda uart, *args, **kwargs: sent.append((args, kwargs))}
    tree = ast.parse(AI_PATH.read_text(encoding='utf-8'))
    classes = [n for n in tree.body if isinstance(n, ast.ClassDef)]
    exec(compile(ast.Module(body=classes, type_ignores=[]), str(AI_PATH), 'exec'), namespace)
    return namespace, sent


class RecognitionLockTests(unittest.TestCase):
    def test_no_result_does_not_stop_next_recognition(self):
        cases = [
            ('mnist', 'MNIST', 2, 2), ('yolo_detect', 'YOLO', 3, 2),
            ('face_detect', 'FACE_DETECT', 4, 2), ('fcr', 'Face_recogization', 5, 3),
            ('slc', 'Self_learning_classfier', 6, 3), ('color', 'Color_recognization', 7, 3),
            ('qrcode', 'QRCode_recognization', 8, 3), ('kpu_model', 'KPU_MODEL', 11, 3),
            ('track', 'Track', 12, 2), ('color_statistics', 'Color_Statistics', 13, 2),
            ('color_extracto', 'Color_Extracto', 14, 2), ('apriltag', 'Apriltag', 15, 2),
            ('kpu_yolo_model', 'KPU_YOLO_MODEL', 16, 3),
        ]
        for attr, cls, mode, cmd_type in cases:
            with self.subTest(mode=mode):
                camera, ns = load_camera()
                ai, sent = load_ai_classes()
                ns.update(DEBUG=False, AI=ai['AI'])
                obj = object.__new__(ai[cls])
                obj.uart, obj.lock = object(), False
                obj.CommandList = ai['AI']['kpu_model' if mode == 11 else 'kpu_model_yolo']
                setattr(camera, attr, obj)
                camera.mode = mode
                camera._dispatch_uart_packet(bytes([0xbb, 0xaa, 1, mode, cmd_type, 0xff] + [0]*8))
                (obj.predict if mode == 6 else obj.recognize)()
                self.assertTrue(sent, 'recognition request suppressed after empty result')
                self.assertFalse(obj.lock)
                if mode == 4:
                    self.assertEqual(obj.face_num, 0)

    def test_successful_variable_reply_does_not_stop_next_recognition(self):
        cases = [('qrcode', 'QRCode_recognization', 8, 3, "[1, 'hello']"),
                 ('track', 'Track', 12, 2, '1|2|3|4|5|6|7|8'),
                 ('color_statistics', 'Color_Statistics', 13, 2, '[1,2,3,4,5,6,7,8]'),
                 ('apriltag', 'Apriltag', 15, 2, '1|2')]
        for attr, cls, mode, cmd_type, payload in cases:
            with self.subTest(mode=mode):
                camera, ns = load_camera()
                ai, sent = load_ai_classes()
                ns.update(DEBUG=False, AI=ai['AI'], hammingWeight=lambda value: 1)
                obj = object.__new__(ai[cls])
                obj.uart, obj.lock = object(), False
                setattr(camera, attr, obj)
                camera.mode = mode
                packet = bytes([0xbb, 0xaa, 2, mode, cmd_type] + [0]*16) + payload.encode() + b'\x00'
                camera._dispatch_uart_packet(packet)
                obj.recognize()
                self.assertTrue(sent, 'recognition request suppressed after successful result')
                self.assertFalse(obj.lock)

    def test_initial_face_count_is_zero(self):
        ai, _ = load_ai_classes()
        face = ai['FACE_DETECT'](object())
        self.assertEqual(face.face_num, 0)

    def test_reply_does_not_unlock_model_stopped_for_switch(self):
        camera, ns = load_camera()
        ns['DEBUG'] = False
        camera.mode = 4
        camera.face_detect = SimpleNamespace(lock=True, face_num=2, max_score=0.8)
        camera._dispatch_uart_packet(bytes([0xbb, 0xaa, 1, 4, 2, 0xff] + [0]*8))
        self.assertTrue(camera.face_detect.lock)

    def test_no_qrcode_result_clears_old_info(self):
        camera, ns = load_camera()
        ns['DEBUG'] = False
        camera.mode = 8
        camera.qrcode = SimpleNamespace(lock=False, id=1, info='old')
        camera._dispatch_uart_packet(bytes([0xbb, 0xaa, 1, 8, 3, 0xff] + [0]*8))
        self.assertIsNone(camera.qrcode.id)
        self.assertIsNone(camera.qrcode.info)


if __name__ == '__main__':
    unittest.main()
