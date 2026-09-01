"""智能家居本地规则运行时。

规则由 mPython 图形化生成并保存在乐动掌控 2.0 程序中。该模块只负责单条件
求值、防抖、冷却和调用主控的统一端点动作，不修改 educore 或底层驱动。
"""

import time
try:
    import ujson
except ImportError:
    import json as ujson


try:
    _ticks_ms = time.ticks_ms
    _ticks_diff = time.ticks_diff
except AttributeError:
    # 仅用于没有 MicroPython ticks API 的主机兼容测试；上板使用 time.ticks_*。
    def _ticks_ms():
        return int(time.time() * 1000)

    def _ticks_diff(current, previous):
        return current - previous


class SmartHomeRules:
    """在 MQTT 辅助模块的 scheduled tick 中执行本地规则。"""

    def __init__(self, owner, now_ms=None):
        self.owner = owner
        self.rules = {}
        self._now_ms = now_ms or _ticks_ms

    def _announce(self, rule):
        """规则上报是平台展示附加能力，失败时不能阻断本地控制。"""
        if hasattr(self.owner, 'publish_rule'):
            try:
                # smart_home_mqtt 会缓存未确认的规则，并在重连时再次发送。
                self.owner.publish_rule(rule)
            except Exception:
                pass

    def add_rule(self, rule, reader):
        if not isinstance(rule, dict) or not callable(reader):
            raise ValueError('invalid_rule')
        rule_id = rule.get('rule_id')
        trigger = rule.get('trigger')
        actions = rule.get('actions')
        if not rule_id or not isinstance(trigger, dict) or not isinstance(actions, list) or not actions:
            raise ValueError('invalid_rule')
        # 复制 JSON，避免图形化生成的临时字典被后续代码意外修改。
        normalized = ujson.loads(ujson.dumps(rule))
        existing = self.rules.get(rule_id)
        if existing is not None:
            existing['rule'] = normalized
            existing['reader'] = reader
            existing['was_matched'] = False
            existing['candidate_since'] = None
            existing['fired_active'] = False
            existing['last_run_at'] = None
            existing['last_skip_at'] = None
            self._announce(normalized)
            return
        self.rules[rule_id] = {
            'rule': normalized,
            'reader': reader,
            'was_matched': False,
            'candidate_since': None,
            'fired_active': False,
            'last_run_at': None,
            'last_skip_at': None
        }
        self._announce(normalized)

    def remove_rule(self, rule_id):
        if rule_id in self.rules:
            del self.rules[rule_id]

    def _compare(self, actual, operator, expected):
        try:
            if operator == '>':
                return actual > expected
            if operator == '>=':
                return actual >= expected
            if operator == '<':
                return actual < expected
            if operator == '<=':
                return actual <= expected
            if operator == '==':
                return actual == expected
            if operator == '!=':
                return actual != expected
        except Exception:
            return False
        return False

    def _event(self, rule_id, status, action_count=0, error=None):
        data = {
            'rule_id': rule_id,
            'status': status,
            'action_count': action_count
        }
        if error:
            data['error'] = error
        if hasattr(self.owner, 'publish_event'):
            endpoint_id = getattr(self.owner, 'rule_endpoint_id', 'controller_01')
            self.owner.publish_event(endpoint_id, 'rule_executed', data)

    def _execute(self, entry, now):
        rule = entry['rule']
        rule_id = rule.get('rule_id')
        actions = rule.get('actions') or []
        action_data = []
        try:
            for action in actions:
                result = self.owner.execute_action(
                    action.get('endpoint_id'),
                    action.get('action'),
                    action.get('data') or {}
                )
                action_data.append(result if result is not None else {})
            entry['last_run_at'] = now
            entry['last_skip_at'] = None
            self._event(rule_id, 'success', len(actions))
        except Exception:
            entry['last_run_at'] = now
            self._event(rule_id, 'failed', len(action_data), 'execute_failed')

    def _tick_rule(self, entry, now):
        rule = entry['rule']
        if rule.get('enabled', True) is False:
            entry['was_matched'] = False
            entry['candidate_since'] = None
            entry['fired_active'] = False
            return
        try:
            actual = entry['reader']()
        except Exception:
            if not entry['was_matched']:
                self._event(rule.get('rule_id'), 'failed', 0, 'read_failed')
            entry['was_matched'] = False
            entry['candidate_since'] = None
            entry['fired_active'] = False
            return

        trigger = rule.get('trigger') or {}
        matched = self._compare(actual, trigger.get('operator'), trigger.get('value'))
        if not matched:
            entry['was_matched'] = False
            entry['candidate_since'] = None
            entry['fired_active'] = False
            entry['last_skip_at'] = None
            return

        if not entry['was_matched']:
            entry['was_matched'] = True
            entry['candidate_since'] = now
            entry['fired_active'] = False

        if entry['fired_active']:
            return
        debounce_ms = rule.get('debounce_ms', 0) or 0
        if _ticks_diff(now, entry['candidate_since']) < debounce_ms:
            return

        cooldown_ms = rule.get('cooldown_ms', 0) or 0
        last_run_at = entry.get('last_run_at')
        if last_run_at is not None and _ticks_diff(now, last_run_at) < cooldown_ms:
            entry['fired_active'] = True
            if entry.get('last_skip_at') is None:
                entry['last_skip_at'] = now
                self._event(rule.get('rule_id'), 'skipped', 0, 'cooldown')
            return

        entry['fired_active'] = True
        self._execute(entry, now)

    def tick(self):
        """执行一次非中断规则轮询；返回本次已处理的规则数量。"""
        now = self._now_ms()
        count = 0
        for entry in self.rules.values():
            self._tick_rule(entry, now)
            count += 1
        return count
