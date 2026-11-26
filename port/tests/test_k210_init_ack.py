"""Mode changes must only succeed after the camera confirms initialization."""
import unittest
from types import SimpleNamespace

from test_k210_mode_switch_race import load_camera


def ack(mode, status, nonce=1):
    return bytes([0xbb, 0xaa, 1, 1, 0xfd, mode, status, nonce, 0, 0, 0, 0])


class InitAckTests(unittest.TestCase):
    def camera(self, replies):
        camera, ns = load_camera()
        del camera._wait_mode_init
        camera.uart = object()
        ns['AI_Uart_CMD'] = lambda *args, **kwargs: None
        target = ns['OBJECT_RECOGNIZATION_MODE']
        camera._yolo_detect_init = lambda choice: setattr(camera, 'mode', target)
        packets = iter(replies)
        camera._read_uart_packet = lambda control=False: next(packets, None)
        camera._wait_for_ai_init_locked = lambda: False
        return camera, ns, target

    def test_switch_waits_for_matching_success(self):
        camera, ns, target = self.camera([ack(3, 1, 0), ack(4, 1), ack(3, 1)])
        self.assertTrue(camera.switcher_mode(target, 1))
        self.assertTrue(camera.connected)
        self.assertEqual(camera.mode, target)

    def test_switch_rejects_device_failure(self):
        camera, ns, target = self.camera([ack(3, 0)])
        with self.assertRaisesRegex(RuntimeError, 'initializ'):
            camera.switcher_mode(target, 1)
        self.assertFalse(camera.connected)
        self.assertFalse(camera.lock)

    def test_missing_ack_is_not_success(self):
        camera, ns, target = self.camera([])
        with self.assertRaisesRegex(RuntimeError, 'timed out'):
            camera.switcher_mode(target, 1)
        self.assertFalse(camera.connected)

    def test_same_mode_request_does_not_assume_object_is_healthy(self):
        camera, ns = load_camera()
        del camera._wait_mode_init
        camera.uart = object()
        ns['AI_Uart_CMD'] = lambda *args, **kwargs: None
        calls = []
        camera._request_mode_exit = lambda mode: calls.append(mode)
        camera._face_detect_init = lambda choice: setattr(camera, 'mode', ns['FACE_DETECTION_MODE'])
        packets = iter([ack(4, 1)])
        camera._read_uart_packet = lambda control=False: next(packets, None)
        self.assertTrue(camera.switcher_mode(ns['FACE_DETECTION_MODE'], 1))
        self.assertEqual(calls, [ns['FACE_DETECTION_MODE']])

    def test_direct_initializer_waits_for_ack(self):
        camera, ns = load_camera()
        del camera._wait_mode_init
        ns['AI_Uart_CMD'] = lambda *args, **kwargs: None
        ns['FACE_DETECT'] = lambda uart, choice: SimpleNamespace(lock=False)
        camera.uart = object()
        camera._read_uart_packet = lambda control=False: None
        camera._wait_for_ai_init_locked = lambda: False
        with self.assertRaisesRegex(RuntimeError, 'timed out'):
            camera.face_detect_init(1)
        self.assertFalse(camera.connected)

    def test_stale_nonce_cannot_confirm_new_request(self):
        camera, ns, target = self.camera([ack(3, 1, 9)])
        with self.assertRaisesRegex(RuntimeError, 'timed out'):
            camera.switcher_mode(target, 1)
        self.assertFalse(camera.connected)


if __name__ == '__main__':
    unittest.main()
