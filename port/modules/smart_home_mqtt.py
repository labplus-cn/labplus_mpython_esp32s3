try:
    import ujson
except ImportError:
    import json as ujson
try:
    import network
except ImportError:
    network = None
import micropython
from machine import Timer
from umqtt.robust import MQTTClient
from smart_home_rules import SmartHomeRules


class _ReconnectAwareMQTTClient(MQTTClient):
    def __init__(self, owner, **kwargs):
        self.owner = owner
        super().__init__(**kwargs)

    def reconnect(self):
        # robust MQTT 会在网络读写失败后同步进入 reconnect()；先执行本地安全
        # 输出，不能等待服务器遗嘱或下一条平台指令。
        self.owner._enter_offline_protection('mqtt_reconnect')
        result = super().reconnect()
        owner = self.owner
        owner.connected = True
        # robust.wait_msg() 可能在 QoS 1 发布等待 PUBACK 时调用 reconnect()。
        # 恢复回调本身还会发布端点/规则消息，若再次进入 reconnect()，旧实现会
        # 递归重复执行恢复回调，最终触发 maximum recursion depth exceeded。
        if owner._reconnect_in_progress:
            return result
        owner._reconnect_in_progress = True
        try:
            owner._restore_command_subscription()
            owner._announce_registered_endpoints(force=True)
            owner._announce_registered_rules(force=True)
            owner._start_command_timer()
        finally:
            owner._reconnect_in_progress = False
            # 解除保护仅恢复后续命令处理，不恢复执行器此前的输出值。
            owner._leave_offline_protection()
        return result


class SmartHomeMQTT:
    def __init__(self, server, client_id, user, password, topic_key, port=1883, keepalive=15):
        self.topic = 'sh/v1/' + topic_key
        self.endpoints = {}
        self.announced_endpoints = set()
        self.connected = False
        self.command_handler = None
        self.command_results = {}
        self.commands_subscribed = False
        self.command_timer = None
        self.command_poll_pending = False
        self.command_timer_generation = 0
        self.safe_outputs = {}
        self.offline_protection_active = False
        self.rule_endpoint_id = 'controller_01'
        self._registered_rules = {}
        self._announced_rules = set()
        self.rule_engine = SmartHomeRules(self)
        # publish() 等待 QoS 1 PUBACK 时，不能让定时轮询读取同一个 socket。
        self._mqtt_io_busy = False
        # 防止 robust.wait_msg() 在恢复回调发布消息时重入同一组恢复回调。
        self._reconnect_in_progress = False
        self.client = _ReconnectAwareMQTTClient(
            self,
            client_id=client_id,
            server=server,
            port=port,
            user=user,
            password=password,
            keepalive=keepalive,
        )
        self.client.set_callback(self._on_message)
        offline = self._encode('status', 'controller_01', {'online': False})
        self.client.set_last_will(self.topic, offline, retain=False, qos=1)

    def _encode(self, channel, endpoint_id, data=None, **fields):
        envelope = {
            'msg_type': 'smart_home',
            'channel': channel,
            'endpoint_id': endpoint_id,
        }
        if data is not None:
            envelope['data'] = data
        for key in fields:
            if fields[key] is not None:
                envelope[key] = fields[key]
        # umqtt.simple 使用 len(msg) 计算 MQTT Remaining Length。必须传入 UTF-8
        # bytes，不能传 str；否则规则名称等中文文本的字符数小于实际字节数，Broker
        # 会按错误长度截断 payload 并报 invalid_json。
        payload = ujson.dumps(envelope)
        return payload.encode('utf-8') if isinstance(payload, str) else payload

    def _publish(self, channel, endpoint_id, data=None, qos=1, **fields):
        message = self._encode(channel, endpoint_id, data, **fields)
        previous_io_busy = self._mqtt_io_busy
        self._mqtt_io_busy = True
        try:
            self.client.publish(self.topic, message, retain=False, qos=qos)
        finally:
            # 重连时可能递归发送端点声明；恢复外层状态，避免保护被提前解除。
            self._mqtt_io_busy = previous_io_busy

    def connect(self):
        self.client.connect()
        self.connected = True
        self._leave_offline_protection()
        self.publish_status('controller_01', True)
        self._announce_registered_endpoints()
        self._announce_registered_rules()

    def disconnect(self):
        self._enter_offline_protection('disconnect')
        try:
            self.client.disconnect()
        finally:
            self.connected = False
            self.announced_endpoints = set()
            self._stop_command_timer()

    def register_safe_output(self, output_id, callback):
        """登记离线保护回调；由图形化执行器积木自动调用。"""
        if not output_id or not callable(callback):
            raise ValueError('invalid_safe_output')
        self.safe_outputs[output_id] = callback

    def _enter_offline_protection(self, _reason=None):
        """网络离线时只执行一次本地安全归零，不依赖 MQTT 可用。"""
        if self.offline_protection_active:
            return
        self.offline_protection_active = True
        for callback in self.safe_outputs.values():
            try:
                callback()
            except Exception:
                # 单个外设归零失败不得阻止其他执行器进入安全状态。
                pass

    def _leave_offline_protection(self):
        # 重连只解除保护锁；执行器保留安全输出，等待新指令或本地规则。
        self.offline_protection_active = False

    def _wifi_is_connected(self):
        if network is None:
            return True
        try:
            return bool(network.WLAN(network.STA_IF).isconnected())
        except Exception:
            # 无法读取网络状态时交由 robust MQTT 的 reconnect() 处理。
            return True

    def register_endpoint(self, endpoint_id, device_type, read_state=None, actions=None, capabilities=None):
        self.endpoints[endpoint_id] = {
            'device_type': device_type,
            'read_state': read_state,
            'actions': actions or {},
            'capabilities': capabilities or {},
        }
        # 图形化程序通常在 connect() 后的循环中调用本方法；首次调用即向服务器声明端点。
        if self.connected:
            self._announce_endpoint(endpoint_id)

    def _registration_data(self, endpoint):
        data = {'device_type': endpoint['device_type']}
        capabilities = endpoint.get('capabilities') or {}
        if not capabilities and endpoint.get('actions'):
            capabilities = {'actions': list(endpoint['actions'].keys())}
        if capabilities:
            data['capabilities'] = capabilities
        return data

    def _announce_endpoint(self, endpoint_id, force=False):
        if not self.connected or endpoint_id not in self.endpoints:
            return
        if not force and endpoint_id in self.announced_endpoints:
            return
        endpoint = self.endpoints[endpoint_id]
        self._publish('register', endpoint_id, self._registration_data(endpoint), qos=1)
        self.announced_endpoints.add(endpoint_id)

    def _announce_registered_endpoints(self, force=False):
        for endpoint_id in self.endpoints:
            self._announce_endpoint(endpoint_id, force=force)

    def publish_rule(self, rule):
        """缓存并发布规则元数据；连接建立后会自动补发。"""
        rule_id = rule.get('rule_id') if isinstance(rule, dict) else None
        if not rule_id:
            raise ValueError('invalid_rule')
        self._registered_rules[rule_id] = ujson.loads(ujson.dumps(rule))
        if self.connected:
            self._publish('rule', self.rule_endpoint_id, self._registered_rules[rule_id], qos=1)
            self._announced_rules.add(rule_id)

    def _announce_registered_rules(self, force=False):
        if not self.connected:
            return
        for rule_id in self._registered_rules:
            if force or rule_id not in self._announced_rules:
                self._publish('rule', self.rule_endpoint_id, self._registered_rules[rule_id], qos=1)
                self._announced_rules.add(rule_id)

    def add_rule(self, rule, reader):
        """登记一条本地规则；reader 返回 trigger 当前字段值。"""
        self.rule_engine.add_rule(rule, reader)

    def execute_action(self, endpoint_id, action, data):
        """执行端点动作，供 MQTT set 和本地规则共用同一动作注册表。"""
        endpoint = self.endpoints.get(endpoint_id)
        if not endpoint:
            raise ValueError('device_not_found')
        handler = endpoint['actions'].get(action)
        if not handler:
            raise ValueError('unsupported_action')
        return handler(data or {})

    def publish_state(self, endpoint_id, data):
        self._publish('state', endpoint_id, data)

    def publish_event(self, endpoint_id, event, data=None):
        self._publish('event', endpoint_id, data, event=event)

    def publish_status(self, endpoint_id, online, reason=None):
        self._publish('status', endpoint_id, {'online': bool(online)}, reason=reason)

    def publish_ack(self, endpoint_id, command_id, ok, data=None, error=None):
        self._publish('ack', endpoint_id, data, id=command_id, ok=bool(ok), error=error)

    def subscribe_commands(self):
        self.commands_subscribed = True
        self.client.subscribe(self.topic, qos=1)
        self._start_command_timer()

    def _restore_command_subscription(self):
        if self.commands_subscribed:
            self.client.subscribe(self.topic, qos=1)

    # MQTT 收包可能触发 Python 回调和网络写入，不能直接在硬件定时器中断中执行。
    # 定时器仅把一次轮询排入 MicroPython 调度队列，让图形化程序无需再手写“一直重复”。
    def _start_command_timer(self):
        if not self.commands_subscribed or self.command_timer is not None:
            return
        self.command_timer_generation += 1
        self.command_timer = Timer(-1)
        self.command_timer.init(
            period=100,
            mode=Timer.PERIODIC,
            callback=self._queue_command_poll
        )

    def _stop_command_timer(self):
        self.command_timer_generation += 1
        self.command_poll_pending = False
        timer = self.command_timer
        self.command_timer = None
        if timer is not None:
            timer.deinit()

    def _queue_command_poll(self, _timer):
        if self.command_poll_pending:
            return
        self.command_poll_pending = True
        try:
            micropython.schedule(self._scheduled_command_poll, self.command_timer_generation)
        except RuntimeError:
            # 调度队列暂满时丢弃本次 tick；下一次定时器触发会继续尝试。
            self.command_poll_pending = False

    def _scheduled_command_poll(self, generation):
        if generation != self.command_timer_generation:
            return
        try:
            if self.connected and self.commands_subscribed and not self._mqtt_io_busy:
                # Wi-Fi 已断开时不要进入 robust.check_msg() 的阻塞重连循环；
                # 立即使本地执行器进入安全状态，下一次 tick 继续等待网络恢复。
                if not self._wifi_is_connected():
                    self._enter_offline_protection('wifi_offline')
                    return
                # Wi-Fi 恢复后允许后续命令再次驱动执行器；这里不恢复任何旧输出。
                self._leave_offline_protection()
                self.check_msg()
                self.rule_engine.tick()
        finally:
            if generation == self.command_timer_generation:
                self.command_poll_pending = False

    def set_command_handler(self, callback):
        self.command_handler = callback

    def _on_message(self, topic, message):
        if topic != self.topic.encode('utf-8') and topic != self.topic:
            return
        try:
            envelope = ujson.loads(message)
        except Exception:
            return
        if envelope.get('msg_type') != 'smart_home' or envelope.get('channel') != 'set':
            return
        endpoint_id = envelope.get('endpoint_id')
        command_id = envelope.get('id')
        action = envelope.get('action')
        data = envelope.get('data')
        if not endpoint_id or not command_id or not action or not isinstance(data, dict):
            return
        if command_id in self.command_results:
            result = self.command_results[command_id]
            self.publish_ack(endpoint_id, command_id, result['ok'], result.get('data'), result.get('error'))
            return
        try:
            result = self.execute_action(endpoint_id, action, data)
            if result is None:
                result = {}
            self._finish_command(endpoint_id, command_id, True, data=result)
        except Exception as error:
            error_code = str(error)
            if error_code not in ('device_not_found', 'unsupported_action'):
                error_code = 'execute_failed'
            self._finish_command(endpoint_id, command_id, False, error=error_code)

    def _finish_command(self, endpoint_id, command_id, ok, data=None, error=None):
        result = {'ok': ok, 'data': data, 'error': error}
        self.command_results[command_id] = result
        self.publish_ack(endpoint_id, command_id, ok, data, error)

    def check_msg(self):
        return self.client.check_msg()
