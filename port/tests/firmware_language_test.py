"""Host-side checks for the build-selected K230 language."""

import importlib
import importlib.util
import pathlib
import sys
import tempfile
import types
import unittest


PORT = pathlib.Path(__file__).resolve().parents[1]
BOARDS = ("labplus_Ledong_v2", "mpython_v3")


def load_source_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class FirmwareLanguageTests(unittest.TestCase):
    def stage(self, directory, language):
        from port.filesystem_image import stage_file_system

        source = directory / "source"
        (source / "file_system/common/lib").mkdir(parents=True)
        (source / "file_system/mpython_v3").mkdir(parents=True)
        (source / "en/labplus_mpython_pro/lib").mkdir(parents=True)
        (source / "file_system/common/marker.txt").write_text("common", encoding="utf-8")
        (source / "file_system/common/lib/firmware_language.py").write_text("IS_ENGLISH = True\n", encoding="utf-8")
        (source / "file_system/mpython_v3/marker.txt").write_text("board", encoding="utf-8")
        (source / "en/labplus_mpython_pro/marker.txt").write_text("english", encoding="utf-8")
        (source / "en/labplus_mpython_pro/lib/firmware_language.py").write_text(
            "IS_ENGLISH = False\n", encoding="utf-8"
        )
        staged = directory / "staged"
        stage_file_system(source, "mpython_v3", language, staged)
        return staged

    def test_staging_selects_language_and_preserves_overlay_order(self):
        for language, marker, is_english in (
            ("zh", "board", False),
            ("en", "english", True),
        ):
            with self.subTest(language=language), tempfile.TemporaryDirectory() as temp:
                staged = self.stage(pathlib.Path(temp), language)
                self.assertEqual((staged / "marker.txt").read_text(encoding="utf-8"), marker)
                name = "firmware_language_test_" + language
                module = load_source_module(name, staged / "lib/firmware_language.py")
                try:
                    self.assertEqual(module.IS_ENGLISH, is_english)
                    self.assertEqual(module.LANGUAGE, language)
                finally:
                    sys.modules.pop(name, None)

    def test_staging_rejects_unknown_language(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                self.stage(pathlib.Path(temp), "fr")

    def test_common_driver_uses_build_default_and_keeps_runtime_override(self):
        for language, initial, changed in (
            ("zh", "人", "person"),
            ("en", "person", "人"),
        ):
            with self.subTest(language=language), tempfile.TemporaryDirectory() as temp:
                staged = self.stage(pathlib.Path(temp), language)
                sys.path.insert(0, str(staged / "lib"))
                sys.modules.pop("firmware_language", None)
                name = "k230_common_test_" + language
                try:
                    driver = load_source_module(name, PORT / "file_system/common/lib/k230_ai/public.py")
                    labels = driver.YOLO80_LABELS
                    self.assertEqual(labels[0], initial)
                    self.assertEqual(driver.USE_ENGLISH_VOICE, language == "en")
                    self.assertEqual(driver.FACE_LANDMARK_EXPRESSION[0], "normal" if language == "en" else "正常")
                    self.assertEqual(driver.MODE[0], "default" if language == "en" else "默认")
                    driver.set_voice_language(language == "zh")
                    self.assertIs(driver.YOLO80_LABELS, labels)
                    self.assertEqual(labels[0], changed)
                finally:
                    sys.modules.pop(name, None)
                    sys.modules.pop("firmware_language", None)
                    sys.path.remove(str(staged / "lib"))

    def test_frozen_board_consumers_follow_build_language(self):
        for board in BOARDS:
            for language, expected_yolo, expected_expression in (
                ("zh", "人", "正常"),
                ("en", "person", "normal"),
            ):
                with self.subTest(board=board, language=language), tempfile.TemporaryDirectory() as temp:
                    staged = self.stage(pathlib.Path(temp), language)
                    sys.path.insert(0, str(staged / "lib"))
                    sys.modules.pop("firmware_language", None)
                    package_name = "k230_frozen_test_{}_{}".format(board, language)
                    package = types.ModuleType(package_name)
                    package.__path__ = [str(PORT / "boards" / board / "modules/educore/k230_ai")]
                    sys.modules[package_name] = package
                    try:
                        public = importlib.import_module(package_name + ".public")
                        ai = importlib.import_module(package_name + ".ai")

                        class FakeUart:
                            def write(self, data):
                                return len(data)

                        yolo = ai.YOLO80(FakeUart())
                        expression = ai.FaceExpressionDetct(FakeUart())
                        self.assertEqual(yolo.category_list[0], expected_yolo)
                        self.assertEqual(yolo.category_list[31], "snowboard" if language == "en" else "单板滑雪板")
                        self.assertEqual(yolo.category_list[34], "baseball bat" if language == "en" else "棒球棒")
                        self.assertEqual(expression.expression_str, expected_expression)
                        self.assertEqual(public.MODE[0], "default" if language == "en" else "默认")
                        self.assertEqual(public.DYNAMIC_GESTURE_STR[0], "up" if language == "en" else "上")
                        public.set_voice_language(language == "zh")
                        self.assertIs(yolo.category_list, public.YOLO80_LABELS)
                        self.assertEqual(yolo.category_list[0], "person" if language == "zh" else "人")
                        self.assertEqual(public.FACE_LANDMARK_EXPRESSION_LABELS[0],
                                         "normal" if language == "zh" else "正常")
                    finally:
                        for suffix in (".ai", ".public", ""):
                            sys.modules.pop(package_name + suffix, None)
                        sys.modules.pop("firmware_language", None)
                        sys.path.remove(str(staged / "lib"))


if __name__ == "__main__":
    unittest.main()
