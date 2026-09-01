"""智能家居本地规则运行时的 MicroPython 兼容行为测试。"""

import os
import sys

try:
    import ujson
except ImportError:
    # 主机冒烟测试没有 MicroPython 的 ujson 时使用同等 JSON 接口；上板仍走 ujson。
    import json as ujson

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'modules')))
from smart_home_rules import SmartHomeRules


class FakeHome:
    def __init__(self):
        self.actions = []
        self.rules = []
        self.events = []
        self.fail_action = False

    def publish_rule(self, rule):
        self.rules.append(rule)

    def execute_action(self, endpoint_id, action, data):
        if self.fail_action:
            raise ValueError('motor_failed')
        self.actions.append((endpoint_id, action, data))
        return data

    def publish_event(self, endpoint_id, event, data=None):
        self.events.append((endpoint_id, event, data))


class FailingAnnouncementHome(FakeHome):
    """模拟平台暂时不可用，确认规则仍能在主控本地执行。"""

    def publish_rule(self, rule):
        raise OSError('broker_unavailable')


def test_rule_waits_for_debounce_and_runs_once():
    now = [0]
    values = [29, 29, 29, 29]
    home = FakeHome()
    rules = SmartHomeRules(home, now_ms=lambda: now[0])
    rule = {
        'rule_id': 'rule_01',
        'name': '温度高时开启风扇',
        'enabled': True,
        'trigger': {
            'endpoint_id': 'temp_humi_01',
            'field': 'temperature_c',
            'operator': '>',
            'value': 28
        },
        'actions': [{
            'endpoint_id': 'fan_01',
            'action': 'set_speed',
            'data': {'speed_pct': 60}
        }],
        'debounce_ms': 1000,
        'cooldown_ms': 5000
    }

    rules.add_rule(rule, lambda: values.pop(0))
    assert len(home.rules) == 1
    rules.tick()
    assert home.actions == []
    now[0] = 999
    rules.tick()
    assert home.actions == []
    now[0] = 1000
    rules.tick()
    assert home.actions == [('fan_01', 'set_speed', {'speed_pct': 60})]
    now[0] = 2000
    rules.tick()
    assert len(home.actions) == 1
    assert home.events[-1][1] == 'rule_executed'
    assert home.events[-1][2]['status'] == 'success'


def test_cooldown_blocks_retrigger_until_next_window():
    now = [0]
    values = [True, False, True, False, True]
    home = FakeHome()
    rules = SmartHomeRules(home, now_ms=lambda: now[0])
    rule = {
        'rule_id': 'rule_presence',
        'name': '有人开灯',
        'enabled': True,
        'trigger': {
            'endpoint_id': 'pir_01',
            'field': 'presence',
            'operator': '==',
            'value': True
        },
        'actions': [{
            'endpoint_id': 'relay_01',
            'action': 'set_power',
            'data': {'power': True}
        }],
        'debounce_ms': 0,
        'cooldown_ms': 1000
    }
    rules.add_rule(rule, lambda: values.pop(0))
    rules.tick()
    now[0] = 100
    rules.tick()
    now[0] = 900
    rules.tick()
    assert len(home.actions) == 1
    assert home.events[-1][2]['status'] == 'skipped'
    now[0] = 1100
    rules.tick()
    now[0] = 1200
    rules.tick()
    assert len(home.actions) == 2


def test_failed_action_is_reported_and_rule_can_be_updated_without_duplicate_registration():
    now = [0]
    home = FakeHome()
    home.fail_action = True
    rules = SmartHomeRules(home, now_ms=lambda: now[0])
    rule = {
        'rule_id': 'rule_fail',
        'name': '测试失败',
        'enabled': True,
        'trigger': {
            'endpoint_id': 'temp_humi_01',
            'field': 'temperature_c',
            'operator': '>=',
            'value': 30
        },
        'actions': [{
            'endpoint_id': 'fan_01',
            'action': 'set_speed',
            'data': {'speed_pct': 20}
        }],
        'debounce_ms': 0,
        'cooldown_ms': 0
    }
    rules.add_rule(rule, lambda: 30)
    rules.add_rule(ujson.loads(ujson.dumps(rule)), lambda: 31)
    assert len(rules.rules) == 1
    assert len(home.rules) == 2
    rules.tick()
    assert home.events[-1][2]['status'] == 'failed'
    assert home.events[-1][2]['error'] == 'execute_failed'


def test_announcement_failure_does_not_stop_local_rule():
    now = [0]
    home = FailingAnnouncementHome()
    rules = SmartHomeRules(home, now_ms=lambda: now[0])
    rule = {
        'rule_id': 'rule_offline_platform',
        'name': '平台离线时仍开风扇',
        'enabled': True,
        'trigger': {
            'endpoint_id': 'temp_humi_01',
            'field': 'temperature_c',
            'operator': '>',
            'value': 28
        },
        'actions': [{
            'endpoint_id': 'fan_01',
            'action': 'set_speed',
            'data': {'speed_pct': 40}
        }],
        'debounce_ms': 0,
        'cooldown_ms': 0
    }
    rules.add_rule(rule, lambda: 30)
    rules.tick()
    assert home.actions == [('fan_01', 'set_speed', {'speed_pct': 40})]


test_rule_waits_for_debounce_and_runs_once()
test_cooldown_blocks_retrigger_until_next_window()
test_failed_action_is_reported_and_rule_can_be_updated_without_duplicate_registration()
test_announcement_failure_does_not_stop_local_rule()
print('smart-home rules MicroPython-compatible tests passed')
