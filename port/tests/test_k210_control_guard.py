"""Control operations must not race for ownership of the UART."""
import threading
from types import SimpleNamespace
import unittest
from test_k210_mode_switch_race import load_camera

ARGS = (1, 1, 1, 320, 240, 0, 0, 0, 0, 0, 0, 0, 0, 0)


class ControlGuardTests(unittest.TestCase):
    def camera(self):
        camera, ns = load_camera()
        camera.uart = object()
        camera._control_guard = threading.Lock()
        ns['AI'] = {'sensor': [100, 1]}
        return camera, ns

    def test_sensor_config_rejected_during_mode_switch_without_clearing_lock(self):
        camera, ns = self.camera()
        ns['AI_Uart_CMD_String'] = lambda **kwargs: None
        def exiting(mode):
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                camera.sensor_config(*ARGS)
            self.assertTrue(camera.lock)
        camera._request_mode_exit = exiting
        camera._yolo_detect_init = lambda choice: setattr(camera, 'mode', 3)
        self.assertTrue(camera.switcher_mode(3, 1))
        self.assertFalse(camera.lock)

    def test_control_guard_prevents_two_simultaneous_sensor_configs(self):
        camera, ns = self.camera()
        entered = threading.Event()
        release = threading.Event()
        exceptions = []
        def send(**kwargs):
            entered.set()
            if not release.wait(3):
                raise RuntimeError('test timed out')
        ns['AI_Uart_CMD_String'] = send
        def run():
            try:
                camera.sensor_config(*ARGS)
            except Exception as error:
                exceptions.append(error)
        worker = threading.Thread(target=run)
        worker.start()
        try:
            self.assertTrue(entered.wait(3))
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                camera.sensor_config(*ARGS)
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                camera.switcher_mode(3, 1)
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                camera.wait_for_ai_init()
            self.assertTrue(camera.lock)
        finally:
            release.set()
            worker.join(3)
        self.assertFalse(worker.is_alive())
        self.assertFalse(exceptions)
        self.assertFalse(camera.lock)

    def test_sensor_send_exception_releases_control(self):
        camera, ns = self.camera()
        def fail(**kwargs):
            raise OSError('uart failure')
        ns['AI_Uart_CMD_String'] = fail
        with self.assertRaisesRegex(OSError, 'uart failure'):
            camera.sensor_config(*ARGS)
        self.assertFalse(camera.lock)
        self.assertTrue(camera._control_guard.acquire(False))
        camera._control_guard.release()
        ns['AI_Uart_CMD_String'] = lambda **kwargs: None
        camera.sensor_config(*ARGS)

    def test_switch_exception_releases_control(self):
        camera, ns = self.camera()
        def fail(mode):
            raise RuntimeError('exit failed')
        camera._request_mode_exit = fail
        camera._wait_for_ai_init_locked = lambda: False
        with self.assertRaises(RuntimeError):
            camera.switcher_mode(3, 1)
        self.assertFalse(camera.lock)
        self.assertTrue(camera._control_guard.acquire(False))
        camera._control_guard.release()

    def test_reset_serializes_controls_and_releases_after_exception(self):
        camera, ns = self.camera()
        ns['AI_Uart_CMD_String'] = lambda **kwargs: None
        def reset():
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                camera.sensor_config(*ARGS)
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                camera.switcher_mode(3, 1)
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                camera.wait_for_ai_init()
            raise OSError('reset failed')
        camera._wait_for_ai_init_locked = reset
        with self.assertRaisesRegex(OSError, 'reset failed'):
            camera.wait_for_ai_init()
        self.assertFalse(camera.lock)
        self.assertTrue(camera._control_guard.acquire(False))
        camera._control_guard.release()

    def test_switch_can_recover_without_reentering_control_guard(self):
        camera, ns = self.camera()
        camera._request_mode_exit = lambda mode: (_ for _ in ()).throw(OSError('lost'))
        camera._wait_for_ai_init_locked = lambda: True
        camera._yolo_detect_init = lambda choice: setattr(camera, 'mode', 3)
        self.assertTrue(camera.switcher_mode(3, 1))
        self.assertFalse(camera.lock)


if __name__ == '__main__':
    unittest.main()
