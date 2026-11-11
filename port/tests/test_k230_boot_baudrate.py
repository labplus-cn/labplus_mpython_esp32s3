"""用 UART/Timer 替身验证启动扫描，不依赖 CanMV 或 ESP32 硬件。"""

import importlib.util
from pathlib import Path
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


RECEIVER = Path(__file__).parents[1] / "file_system/common/smartcamera_k230.py"


def handshake(command=1, kind=255):
    data = bytes((0xBB, 0xAA, 1, command, kind, 0, 0, 0, 0, 0, 0))
    return data + bytes((sum(data) & 255,))


class BootBaudrateTests(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.requests = 0
        self.reply = lambda uart: [handshake()]
        owner = self

        class UART:
            def __init__(self, number, **kwargs):
                self.baudrate = kwargs["baudrate"]
                self.options = kwargs
                self.pending = []
                owner.events.append(("uart", number, self.baudrate))

            def init(self, **kwargs):
                self.baudrate = kwargs["baudrate"]
                self.options.update(kwargs)
                owner.events.append(("switch", self.baudrate))

            def any(self):
                return bool(self.pending)

            def read(self):
                return self.pending.pop(0) if self.pending else None

        class Timer:
            PERIODIC = 1

            def __init__(self, number):
                owner.events.append(("timer", number))

            def init(self, **kwargs):
                owner.events.append(("poll", kwargs["period"]))

        def send(uart, command, kind):
            self.assertEqual((command, kind), (1, 255))
            self.requests += 1
            # 阻止扫描逻辑错误导致测试进程永久等待。
            if self.requests > 400:
                raise AssertionError("scan did not accept a valid handshake")
            self.events.append(("request", uart.baudrate))
            uart.pending.extend(self.reply(uart))

        machine = ModuleType("machine")
        machine.Pin = SimpleNamespace(P1=1, P0=0)
        machine.UART = UART
        machine.Timer = Timer
        public = ModuleType("k230_ai.public")
        public._print_i18n = lambda *args: None
        ai = ModuleType("lib.k230_ai")
        ai.DEFAULT_MODE = 0
        ai.MAX_BUF_SIZE = 4096
        ai.AI_Uart_CMD = send
        modules = {
            "machine": machine,
            "lib": ModuleType("lib"),
            "lib.k230_ai": ai,
            "k230_ai": ModuleType("k230_ai"),
            "k230_ai.public": public,
        }
        spec = importlib.util.spec_from_file_location("test_camera_receiver", RECEIVER)
        self.module = importlib.util.module_from_spec(spec)
        with patch.dict("sys.modules", modules):
            spec.loader.exec_module(self.module)
        self.module.time = SimpleNamespace(sleep=lambda _: None, sleep_ms=lambda _: None)
        self.module.gc = SimpleNamespace(collect=lambda: None)

    def camera(self, **kwargs):
        return self.module.SmartCameraK230(**kwargs)

    def test_fixed_defaults_and_supported_rates_preserve_timer_order(self):
        for rate in (None,) + self.module.SUPPORTED_BAUDRATES:
            with self.subTest(rate=rate):
                self.events.clear()
                camera = self.camera() if rate is None else self.camera(baudrate=rate)
                self.assertEqual(camera.baudrate, 1152000 if rate is None else rate)
                self.assertFalse(camera.lock)
                self.assertEqual([e[0] for e in self.events], ["uart", "request", "timer", "poll"])
                self.assertEqual(self.events[-1], ("poll", 50))

    def test_invalid_values_fail_before_uart_creation(self):
        for value in (True, False, None, 115200.0, "115200", "AUTO", [], {}, -1, 12345):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    self.camera(baudrate=value)
                self.assertEqual(self.events, [])

    def test_auto_finds_every_supported_rate_before_starting_timer(self):
        for target in self.module.SUPPORTED_BAUDRATES:
            with self.subTest(target=target):
                self.events.clear()
                self.requests = 0
                self.reply = lambda uart: [handshake()] if uart.baudrate == target else []
                camera = self.camera(baudrate="auto")
                self.assertEqual(camera.baudrate, target)
                self.assertFalse(camera.lock)
                self.assertEqual(self.events[-3:], [("request", target), ("timer", -1), ("poll", 50)])
                self.assertEqual(sum(e[0] == "uart" for e in self.events), 1)
                self.assertEqual(camera.uart.options["rxbuf"], 4096)

    def test_auto_repeats_scan_for_late_camera_startup(self):
        first_sweep_requests = len(self.module.SUPPORTED_BAUDRATES) * 5
        self.reply = lambda uart: [handshake()] if self.requests > first_sweep_requests else []
        camera = self.camera(baudrate="auto")
        self.assertEqual(self.requests, first_sweep_requests + 1)
        self.assertEqual(camera.baudrate, 1152000)

    def test_corrupt_checksum_and_other_commands_do_not_lock_rate(self):
        bad = handshake()[:-1] + bytes(((handshake()[-1] + 1) & 255,))

        def reply(uart):
            if self.requests == 1:
                return [bad + handshake(command=2) + handshake(kind=1)]
            return [handshake()] if self.requests == 2 else []

        self.reply = reply
        camera = self.camera(baudrate="auto")
        self.assertEqual(self.requests, 2)
        self.assertFalse(camera.lock)

    def test_fragmented_header_and_payload_survive_reads(self):
        self.reply = lambda uart: [b"\x00\xBB", handshake()[1:5], handshake()[5:]] if self.requests == 1 else []
        camera = self.camera(baudrate="auto")
        self.assertEqual(self.requests, 3)
        self.assertFalse(camera.lock)

    def test_previous_rate_partial_packet_cannot_form_a_handshake(self):
        # 首档最后一次读到半包，换档后剩余半包不得与旧速率数据拼接。
        def reply(uart):
            if self.requests == 5:
                return [handshake()[:6]]
            if self.requests == 6:
                return [handshake()[6:]]
            return [handshake()] if self.requests == 7 else []

        self.reply = reply
        camera = self.camera(baudrate="auto")
        self.assertEqual(self.requests, 7)
        self.assertNotEqual(camera.baudrate, 1152000)

    def test_fixed_mode_keeps_its_existing_timeout(self):
        self.reply = lambda uart: []
        camera = self.camera()
        self.assertEqual(self.requests, 200)
        self.assertTrue(camera.lock)
        self.assertFalse(any(e[0] == "switch" for e in self.events))


if __name__ == "__main__":
    unittest.main()
