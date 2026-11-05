"""Regression checks for asynchronous speech-model command setup."""

import pathlib
import unittest


PORT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = PORT / "usercmodule" / "audio" / "src" / "audio_sr.c"
SR_SOURCE = PORT / "usercmodule" / "audio" / "src" / "sr" / "sr.c"


def function_body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError("unterminated function: {}".format(signature))


class SpeechCommandLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_add_waits_for_multinet_before_touching_command_manager(self):
        body = function_body(self.source, "static mp_obj_t mp_esp_mn_commands_add(")
        self.assertLess(
            body.index("wait_for_multinet_ready();"),
            body.index("esp_mn_commands_add(command_id, text)"),
        )

    def test_update_rejects_empty_command_list_before_building_fst(self):
        body = function_body(self.source, "static mp_obj_t mp_esp_mn_commands_update(void)")
        empty_check = body.index("esp_mn_commands_get_from_index(0)")
        fst_update = body.index("esp_mn_commands_update();")
        self.assertLess(empty_check, fst_update)
        self.assertIn("mp_raise_msg", body[empty_check:fst_update])

    def test_silent_wakeup_emits_debug_line_for_both_afe_wake_paths(self):
        source = SR_SOURCE.read_text(encoding="utf-8")
        body = function_body(source, "void detect_Task(void *arg)")
        self.assertEqual(body.count('printf("[SR] Wake word detected\\n")'), 2)
        self.assertIn('strcmp(word, "none") == 0', source)

    def test_english_sr_uses_only_hi_esp_wakenet(self):
        source = SR_SOURCE.read_text(encoding="utf-8")
        body = function_body(source, "void sr_init(")
        english_config = body[body.index("#if defined(MPYTHON_LANGUAGE_EN)"):]
        self.assertIn('esp_srmodel_filter(models, ESP_WN_PREFIX, "hiesp")', english_config)
        self.assertIn("afe_config->wakenet_model_name_2 = NULL;", english_config)


if __name__ == "__main__":
    unittest.main()
