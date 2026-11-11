"""智能家居本地规则运行时。

规则由 mPython 图形化生成并保存在乐动掌控 2.0 程序中。该模块只负责单条件
求值、防抖、冷却和调用主控的统一端点动作，不修改 educore 或底层驱动。
"""

import time
import _thread
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
    """在 scheduled tick 中求值；用户事件回调由有界 FIFO 工作线程执行。"""

    def __init__(self, owner, now_ms=None):
        self.owner = owner
        self.rules = {}
        self._now_ms = now_ms or _ticks_ms
        self._callback_queue = []
        self._callback_lock = _thread.allocate_lock()
        self._callback_worker_running = False
        self._callback_generation = 0
        self.callback_rejections = 0
        self.callback_failures = 0
        self.last_callback_error = None

    def _callback_error(self, event_id, error, rejected=False):
        # Keep diagnostics local: publishing here would race the MQTT socket or
        # synchronously reconnect from the scheduled poll we are protecting.
        if rejected:
            self.callback_rejections += 1
        else:
            self.callback_failures += 1
        self.last_callback_error = {'event_id': event_id, 'error': str(error)}
        print('smart_home callback', event_id, str(error))

    def cancel_callbacks(self):
        """Invalidate queued work on disconnect; an in-flight callback cannot be killed."""
        self._callback_generation += 1

    def _queue_callback(self, entry):
        event_id = entry['rule']['rule_id']
        # A scheduled callback may interrupt the worker holding this lock. Never
        # wait in the producer: that could deadlock MicroPython's scheduler/GIL.
        if not self._callback_lock.acquire(False):
            self._callback_error(event_id, 'callback_queue_busy', True)
            return
        error = None
        try:
            if entry.get('callback_pending'):
                return  # Coalesce edges while this event is queued or running.
            if len(self._callback_queue) >= 8:
                error = 'callback_queue_full'
            else:
                entry['callback_pending'] = True
                self._callback_queue.append((entry, self._callback_generation))
                if not self._callback_worker_running:
                    self._callback_worker_running = True
                    try:
                        _thread.start_new_thread(self._run_callbacks, ())
                    except Exception as exc:
                        self._callback_worker_running = False
                        self._callback_queue.pop()
                        entry['callback_pending'] = False
                        error = 'callback_worker_start_failed: ' + str(exc)
        finally:
            self._callback_lock.release()
        if error:
            # Reject newest, consume this edge, and retain existing FIFO order.
            self._callback_error(event_id, error, True)

    def _run_callbacks(self):
        while True:
            self._callback_lock.acquire()
            try:
                if not self._callback_queue:
                    self._callback_worker_running = False
                    return  # No permanent thread/resource leak after events stop.
                entry, generation = self._callback_queue.pop(0)
            finally:
                self._callback_lock.release()
            event_id = entry['rule']['rule_id']
            callback_started = False
            try:
                # Removed/replaced events and disconnected sessions must not run
                # stale queued cloud commands after a slow earlier callback.
                if (generation == self._callback_generation and
                        self.rules.get(event_id) is entry):
                    begin_callback = getattr(self.owner, '_begin_rule_callback', None)
                    if callable(begin_callback):
                        begin_callback(generation)
                        callback_started = True
                    entry['callback']()
            except BaseException as error:
                # A user SystemExit/KeyboardInterrupt belongs to this task, not
                # the worker lifetime; otherwise later queued work is stranded.
                self._callback_error(event_id, error)
            finally:
                if callback_started:
                    try:
                        self.owner._end_rule_callback(generation)
                    except BaseException as error:
                        self._callback_error(event_id, error)
                entry['callback_pending'] = False

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

    def add_event(self, event_id, reader, operator, value, callback, debounce_ms=500, cooldown_ms=3000):
        """Register a board-local callback; arbitrary callback code is not a platform rule."""
        if (not event_id or not callable(reader) or not callable(callback) or
                operator not in ('>', '>=', '<', '<=', '==', '!=') or
                not isinstance(debounce_ms, int) or debounce_ms < 0 or
                not isinstance(cooldown_ms, int) or cooldown_ms < 0):
            raise ValueError('invalid_rule')
        self.rules[event_id] = {
            'rule': {'rule_id': event_id, 'trigger': {'operator': operator, 'value': value},
                     'debounce_ms': debounce_ms, 'cooldown_ms': cooldown_ms},
            'reader': reader, 'callback': callback, 'was_matched': False,
            'candidate_since': None, 'fired_active': False, 'last_run_at': None,
            'last_skip_at': None
        }

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

    def _event(self, rule_id, status, action_count=0, error=None, actions=None):
        data = {
            'rule_id': rule_id,
            'status': status,
            'source': 'local_rule',
            'action_count': action_count
        }
        if error:
            data['error'] = error
        if actions:
            data['actions'] = actions
        if hasattr(self.owner, 'publish_event'):
            endpoint_id = getattr(self.owner, 'rule_endpoint_id', 'controller_01')
            self.owner.publish_event(endpoint_id, 'rule_executed', data)

    def _execute(self, entry, now):
        rule = entry['rule']
        rule_id = rule.get('rule_id')
        if 'callback' in entry:
            entry['last_run_at'] = now
            entry['last_skip_at'] = None
            self._queue_callback(entry)
            return
        actions = rule.get('actions') or []
        action_data = []
        try:
            for action in actions:
                endpoint_id = action.get('endpoint_id')
                action_name = action.get('action')
                data = action.get('data') or {}
                try:
                    result = self.owner.execute_action(
                        endpoint_id,
                        action_name,
                        data,
                        source='local_rule',
                        rule_id=rule_id
                    )
                except TypeError:
                    result = self.owner.execute_action(
                        endpoint_id,
                        action_name,
                        data
                    )
                action_data.append(result if result is not None else {})
            entry['last_run_at'] = now
            entry['last_skip_at'] = None
            self._event(rule_id, 'success', len(actions), actions=actions)
        except Exception as error:
            err_msg = str(error)
            if err_msg in ('manual_override', 'safety_lock'):
                entry['last_skip_at'] = now
                self._event(rule_id, 'skipped', len(action_data), error=err_msg, actions=actions)
            else:
                entry['last_run_at'] = now
                self._event(rule_id, 'failed', len(action_data), error='execute_failed', actions=actions)

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
                if 'callback' not in entry:
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
                if 'callback' not in entry:
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
