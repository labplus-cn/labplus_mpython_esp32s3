"""Host-side checks for build-selected driver print language."""

import contextlib
import ast
import importlib.util
import io
import pathlib
import sys
import types
import unittest
from unittest.mock import patch


PORT = pathlib.Path(__file__).resolve().parents[1]
HELPER_PATH = PORT / "modules" / "mpython_i18n.py"
BOARD_MPYTHON_FILES = (
    PORT / "boards" / "labplus_Ledong_v2" / "modules" / "mpython.py",
    PORT / "boards" / "mpython_v3" / "modules" / "mpython.py",
)


def load_helper(is_english):
    if not HELPER_PATH.is_file():
        return None

    config = types.ModuleType("firmware_language")
    if is_english is not None:
        config.IS_ENGLISH = is_english
    sys.modules["firmware_language"] = config

    name = "mpython_i18n_test_{}".format(is_english)
    spec = importlib.util.spec_from_file_location(name, HELPER_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class PrintLanguageTests(unittest.TestCase):
    def tearDown(self):
        sys.modules.pop("firmware_language", None)
        for name in ("mpython_i18n_test_True", "mpython_i18n_test_False", "mpython_i18n_test_None"):
            sys.modules.pop(name, None)

    def test_print_uses_firmware_language_and_preserves_print_arguments(self):
        for is_english, expected in (
            (False, "连接失败 timeout!"),
            (True, "Connection failed timeout!"),
            (None, "连接失败 timeout!"),
        ):
            with self.subTest(is_english=is_english):
                helper = load_helper(is_english)
                self.assertIsNotNone(helper, "localized print helper is missing")

                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    helper.print_i18n("连接失败", "Connection failed", "timeout", end="!")

                self.assertEqual(output.getvalue(), expected)

    def test_print_can_preserve_a_callers_print_function(self):
        helper = load_helper(True)
        calls = []

        def caller_print(*args, **kwargs):
            calls.append((args, kwargs))

        try:
            helper.print_i18n("连接失败", "Connection failed", "timeout", end="!", printer=caller_print)
        except TypeError as exc:
            self.fail("localized printer did not accept a caller print function: {}".format(exc))

        self.assertEqual(calls, [(('Connection failed', 'timeout'), {'end': '!'})])


class RTCDateLanguageTests(unittest.TestCase):
    def tearDown(self):
        sys.modules.pop("firmware_language", None)
        for name in ("mpython_i18n_test_True", "mpython_i18n_test_False", "mpython_i18n_test_None"):
            sys.modules.pop(name, None)

    def test_rtcdate_returns_iso_date_and_localized_time(self):
        for board_file in BOARD_MPYTHON_FILES:
            for is_english in (False, True, None):
                with self.subTest(board=board_file.parent.parent.name, is_english=is_english):
                    helper = load_helper(is_english)
                    localized_text = getattr(helper, "localized_text", None)
                    self.assertTrue(callable(localized_text), "localized_text helper is missing")

                    source = board_file.read_text(encoding="utf-8")
                    tree = ast.parse(source, filename=str(board_file))
                    rtcdate_node = next(
                        node for node in tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == "RTCdate"
                    )
                    isolated_module = ast.fix_missing_locations(
                        ast.Module(body=[rtcdate_node], type_ignores=[])
                    )
                    namespace = {"_localized_text": localized_text}
                    exec(compile(isolated_module, str(board_file), "exec"), namespace)

                    machine = types.ModuleType("machine")

                    class FakeRTC:
                        def datetime(self):
                            return (2026, 8, 17, 0, 9, 7, 5, 0)

                    machine.RTC = FakeRTC
                    with patch.dict(sys.modules, {"machine": machine}):
                        rtcdate = namespace["RTCdate"]
                        self.assertEqual(rtcdate(0), "2026-08-17")
                        if is_english is True:
                            self.assertEqual(rtcdate(1), "9Hour:7Minute:5Second")
                        else:
                            self.assertEqual(rtcdate(1), "9时:7分:5秒")


if __name__ == "__main__":
    unittest.main()
