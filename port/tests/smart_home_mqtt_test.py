"""智能家居 MQTT 固件辅助模块的 MicroPython 行为测试。

测试使用 sys.modules 注入伪造的 umqtt.robust 客户端，不连接真实 Broker，
重点验证协议封装、端点命令处理、消息回显过滤和重连后的订阅恢复。
将本文件与 smart_home_mqtt.py 一起复制到掌控板后，可直接运行本文件。
"""

import sys
import os
try:
    import ujson
except ImportError:
    import json as ujson

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'modules')))


class FakeMQTTClient:
    """记录 MQTT 调用结果的最小替身，避免测试依赖网络和硬件。"""

    instances = []

    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.callback = None
        self.published = []
        self.subscriptions = []
        self.last_will = None
        self.check_calls = 0
        self.publish_hook = None
        FakeMQTTClient.instances.append(self)

    def set_callback(self, callback):
        # 保存固件注册的回调，测试通过 callback(...) 模拟 Broker 下发消息。
        self.callback = callback

    def set_last_will(self, topic, message, retain=False, qos=0):
        # 记录遗嘱消息参数，用于验证智能家居协议不使用 retained 状态。
        self.last_will = (topic, message, retain, qos)

    def connect(self):
        return False

    def disconnect(self):
        return None

    def publish(self, topic, message, retain=False, qos=0):
        # 保存发布记录，测试随后解析 ack 内容和校验 QoS/retain。
        self.published.append((topic, message, retain, qos))
        if self.publish_hook is not None:
            self.publish_hook()

    def subscribe(self, topic, qos=0):
        # 保存订阅记录，用于验证首次订阅和重连后的重新订阅。
        self.subscriptions.append((topic, qos))

    def check_msg(self):
        self.check_calls += 1
        return None

    def reconnect(self):
        return True


class FakeModule:
    pass


class FakeTimer:
    """模拟软件定时器；回调先由测试主动触发，避免真实硬件定时中断。"""

    PERIODIC = 1
    instances = []

    def __init__(self, timer_id):
        self.timer_id = timer_id
        self.period = None
        self.mode = None
        self.callback = None
        self.deinitialized = False
        FakeTimer.instances.append(self)

    def init(self, period, mode, callback):
        self.period = period
        self.mode = mode
        self.callback = callback

    def deinit(self):
        self.deinitialized = True

    def fire(self):
        self.callback(self)


class FakeMicroPython:
    """记录 ISR 请求的调度任务，由测试显式执行，验证 MQTT 不直接在定时器回调中运行。"""

    scheduled = []

    @staticmethod
    def schedule(callback, argument):
        FakeMicroPython.scheduled.append((callback, argument))


class FakeNetwork:
    """模拟 STA Wi-Fi 状态，验证网络断开时安全输出不依赖 Broker 回调。"""

    STA_IF = 0
    connected = True

    class _WLAN:
        def isconnected(self):
            return FakeNetwork.connected

    @staticmethod
    def WLAN(_interface):
        return FakeNetwork._WLAN()


# 在导入待测模块前替换其设备端 MQTT 依赖，保持测试可在 MicroPython 中运行。
umqtt_module = FakeModule()
robust_module = FakeModule()
robust_module.MQTTClient = FakeMQTTClient
umqtt_module.robust = robust_module
sys.modules['umqtt'] = umqtt_module
sys.modules['umqtt.robust'] = robust_module

machine_module = FakeModule()
machine_module.Timer = FakeTimer
sys.modules['machine'] = machine_module
sys.modules['micropython'] = FakeMicroPython
sys.modules['network'] = FakeNetwork

import smart_home_mqtt as module


def reset_fake_clients():
    FakeMQTTClient.instances = []
    FakeTimer.instances = []
    FakeMicroPython.scheduled = []
    FakeNetwork.connected = True


def test_set_command_runs_registered_action_and_acknowledges():
    """验证 set 命令能找到端点动作，并返回同一命令 ID 的成功 ack。"""
    reset_fake_clients()
    action_calls = []
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.register_endpoint(
        'fan_01',
        'fan',
        actions={'set_power': lambda data: action_calls.append(data) or {'power': data['power']}}
    )
    home.connect()
    home.subscribe_commands()

    client = FakeMQTTClient.instances[-1]
    # 设备连接后先设置离线遗嘱，再订阅自己的智能家居 Topic。
    assert client.last_will[2] is False
    assert client.subscriptions == [('sh/v1/a1b2cX7kP9', 1)]

    command = ujson.dumps({
        'msg_type': 'smart_home',
        'channel': 'set',
        'endpoint_id': 'fan_01',
        'id': 'cmd-000101',
        'action': 'set_power',
        'data': {'power': True},
    })
    client.callback(b'sh/v1/a1b2cX7kP9', command)

    # 端点动作应执行一次，ack 必须保留原命令 ID 和实际结果。
    assert action_calls == [{'power': True}]
    ack = ujson.loads(client.published[-1][1])
    assert ack['channel'] == 'ack'
    assert ack['id'] == 'cmd-000101'
    assert ack['ok'] is True
    assert ack['data'] == {'power': True}
    assert client.published[-1][2] is False


def test_default_keepalive_detects_abnormal_program_stop_promptly():
    """默认保活应缩短，以便 Ctrl+C 未正常断开时 Broker 能及时判定离线。"""
    reset_fake_clients()
    module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )

    # Aedes 会在客户端长时间无 MQTT 流量时按 keepalive 清理会话；15 秒
    # 是异常中断的兜底，不替代图形化程序 finally 中的即时 disconnect。
    assert FakeMQTTClient.instances[-1].kwargs['keepalive'] == 15


def test_non_set_message_does_not_run_endpoint_action():
    """验证设备收到自身回显的 state 时不会误执行执行器动作。"""
    reset_fake_clients()
    action_calls = []
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.register_endpoint('fan_01', 'fan', actions={'set_power': lambda data: action_calls.append(data)})
    home.connect()
    home.subscribe_commands()

    client = FakeMQTTClient.instances[-1]
    echoed_state = ujson.dumps({
        'msg_type': 'smart_home',
        'channel': 'state',
        'endpoint_id': 'fan_01',
        'data': {'power': True},
    })
    published_count = len(client.published)
    client.callback(b'sh/v1/a1b2cX7kP9', echoed_state)

    assert action_calls == []
    assert len(client.published) == published_count


def test_motor_speed_action_drives_mpython_motor_and_reports_state():
    """验证马达/风扇端点收到 set_speed 后调用 mPython 乐动电机驱动、上报状态并返回 ack。"""
    reset_fake_clients()

    class FakeLedongShield:
        """模拟 mpython.ledong_shield，记录 M1 外接马达/风扇的速度指令。"""

        def __init__(self):
            self.calls = []

        def set_motor(self, motor_num, speed):
            self.calls.append((motor_num, speed))

    ledong_shield = FakeLedongShield()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )

    def set_motor_speed(data):
        speed = int(data.get('speed_pct'))
        ledong_shield.set_motor(1, speed)
        result = {'power': speed != 0, 'speed_pct': speed}
        home.publish_state('motor_01', result)
        return result

    home.register_endpoint('motor_01', 'dc_motor', actions={'set_speed': set_motor_speed})
    home.connect()
    home.subscribe_commands()

    client = FakeMQTTClient.instances[-1]
    command = ujson.dumps({
        'msg_type': 'smart_home',
        'channel': 'set',
        'endpoint_id': 'motor_01',
        'id': 'cmd-000103',
        'action': 'set_speed',
        'data': {'speed_pct': 60},
    })
    client.callback(b'sh/v1/a1b2cX7kP9', command)

    assert ledong_shield.calls == [(1, 60)]
    state = ujson.loads(client.published[-2][1])
    assert state['channel'] == 'state'
    assert state['endpoint_id'] == 'motor_01'
    assert state['data'] == {'power': True, 'speed_pct': 60}
    ack = ujson.loads(client.published[-1][1])
    assert ack['channel'] == 'ack'
    assert ack['id'] == 'cmd-000103'
    assert ack['ok'] is True
    assert ack['data'] == {'power': True, 'speed_pct': 60}


def test_reconnect_restores_command_subscription():
    """验证 robust 重连完成后会重新订阅命令 Topic。"""
    reset_fake_clients()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.connect()
    home.subscribe_commands()

    client = FakeMQTTClient.instances[-1]
    client.reconnect()

    assert client.subscriptions == [
        ('sh/v1/a1b2cX7kP9', 1),
        ('sh/v1/a1b2cX7kP9', 1),
    ]


def test_register_endpoint_announces_once_after_connect():
    """验证首次调用 register_endpoint 会自动发现端点且重复调用不重复上报。"""
    reset_fake_clients()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.connect()
    home.register_endpoint(
        'temp_humi_01',
        'temperature_humidity_sensor',
        actions={'set_calibration': lambda data: data}
    )
    home.register_endpoint(
        'temp_humi_01',
        'temperature_humidity_sensor',
        actions={'set_calibration': lambda data: data}
    )

    client = FakeMQTTClient.instances[-1]
    registrations = []
    for item in client.published:
        envelope = ujson.loads(item[1])
        if envelope.get('channel') == 'register':
            registrations.append((item, envelope))

    assert len(registrations) == 1
    item, envelope = registrations[0]
    assert item[0] == 'sh/v1/a1b2cX7kP9'
    assert item[2] is False
    assert item[3] == 1
    assert envelope['endpoint_id'] == 'temp_humi_01'
    assert envelope['data']['device_type'] == 'temperature_humidity_sensor'
    assert envelope['data']['capabilities']['actions'] == ['set_calibration']


def test_non_ascii_rule_payload_uses_utf8_bytes():
    """规则名称含中文时，MQTT 剩余长度必须按 UTF-8 字节数计算。"""
    reset_fake_clients()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    rule_name = '本地规则：距离大于 100 厘米时开风扇'
    payload = home._encode('rule', 'controller_01', {
        'name': rule_name
    })

    # umqtt.simple 按 len(msg) 写 MQTT Remaining Length；必须传 bytes，
    # 否则字符串中的中文字符数与实际 UTF-8 字节数不一致，Broker 会收到截断 JSON。
    assert len(rule_name.encode('utf-8')) > len(rule_name)
    assert isinstance(payload, bytes)
    assert ujson.loads(payload)['data']['name'].startswith('本地规则')


def test_reconnect_reannounces_registered_endpoints():
    """验证连接重建后会重新声明已有端点，服务器可恢复端点档案。"""
    reset_fake_clients()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.register_endpoint('temp_humi_01', 'temperature_humidity_sensor')
    home.connect()
    client = FakeMQTTClient.instances[-1]
    client.reconnect()

    registrations = []
    for item in client.published:
        if ujson.loads(item[1]).get('channel') == 'register':
            registrations.append(item)
    assert len(registrations) == 2


def test_reconnect_does_not_reenter_owner_restore_callbacks():
    """重连期间 QoS 发布再次触发 reconnect 时，不得递归重新发布注册信息。"""
    reset_fake_clients()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.connect()
    home.register_endpoint('temp_humi_01', 'temperature_humidity_sensor')
    client = FakeMQTTClient.instances[-1]

    # 模拟 robust.wait_msg() 在发布等待 PUBACK 时再次调用同一个 reconnect()。
    # 旧实现会重新进入端点/规则发布，最终触发 maximum recursion depth exceeded。
    client.publish_hook = lambda: client.reconnect()
    client.reconnect()

    registrations = [
        item for item in client.published
        if ujson.loads(item[1]).get('channel') == 'register'
    ]
    assert len(registrations) == 2


def test_subscribe_commands_runs_check_msg_from_one_scheduled_timer_and_restores_after_reconnect():
    """订阅命令后应自动定时处理，不要求图形化程序手写一直重复或 check_msg。"""
    reset_fake_clients()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.connect()
    home.subscribe_commands()

    assert len(FakeTimer.instances) == 1
    timer = FakeTimer.instances[0]
    assert timer.timer_id == -1
    assert timer.period == 100
    assert timer.mode == FakeTimer.PERIODIC

    # 即使中断在主循环未处理时连续触发，也只允许排入一次 MQTT 轮询任务。
    timer.fire()
    timer.fire()
    assert len(FakeMicroPython.scheduled) == 1
    assert FakeMQTTClient.instances[-1].check_calls == 0

    callback, argument = FakeMicroPython.scheduled.pop(0)
    callback(argument)
    assert FakeMQTTClient.instances[-1].check_calls == 1

    home.disconnect()
    assert timer.deinitialized is True

    FakeMQTTClient.instances[-1].reconnect()
    assert len(FakeTimer.instances) == 2
    assert FakeTimer.instances[-1].callback is not None


def test_command_poll_does_not_read_socket_during_publish():
    """验证 QoS 发布等待 PUBACK 时，定时轮询不会并发读取同一个 MQTT socket。"""
    reset_fake_clients()
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.connect()
    home.subscribe_commands()

    client = FakeMQTTClient.instances[-1]
    generation = home.command_timer_generation
    # 模拟真实设备上定时器在 publish() 等待 PUBACK 的窗口触发调度任务。
    client.publish_hook = lambda: home._scheduled_command_poll(generation)
    home.publish_state('controller_01', {'probe': True})

    # 若轮询读取 socket，会与 publish() 的 PUBACK 读取竞争并造成字节错位。
    assert client.check_calls == 0


def test_local_rule_uses_registered_action_and_is_tick_by_existing_timer():
    """验证本地规则复用端点动作，并由已有 MQTT 定时器自动轮询。"""
    reset_fake_clients()
    action_calls = []
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.register_endpoint(
        'fan_01', 'fan',
        actions={'set_speed': lambda data: action_calls.append(data) or data}
    )
    home.connect()
    home.subscribe_commands()
    home.add_rule({
        'rule_id': 'rule_01',
        'name': '温度高时开风扇',
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
        'debounce_ms': 0,
        'cooldown_ms': 0
    }, lambda: 30)

    client = FakeMQTTClient.instances[-1]
    rule_messages = [ujson.loads(item[1]) for item in client.published
                     if ujson.loads(item[1]).get('channel') == 'rule']
    assert len(rule_messages) == 1
    assert rule_messages[0]['data']['rule_id'] == 'rule_01'

    home.command_timer.fire()
    callback, argument = FakeMicroPython.scheduled.pop(0)
    callback(argument)
    assert action_calls == [{'speed_pct': 60}]
    event = ujson.loads(client.published[-1][1])
    assert event['channel'] == 'event'
    assert event['event'] == 'rule_executed'
    assert event['data']['status'] == 'success'


def test_wifi_loss_runs_each_registered_safe_output_once_and_never_restores_it():
    """Wi-Fi 断开后必须本地归零；连接恢复只解除保护，不恢复旧输出。"""
    reset_fake_clients()
    safe_calls = []
    home = module.SmartHomeMQTT(
        server='127.0.0.1',
        client_id='esp32_class_01',
        user='device_user',
        password='device_password',
        topic_key='a1b2cX7kP9',
    )
    home.register_safe_output('fan_01', lambda: safe_calls.append('fan_01'))
    home.register_safe_output('relay_01', lambda: safe_calls.append('relay_01'))
    home.connect()
    home.subscribe_commands()

    FakeNetwork.connected = False
    timer = home.command_timer
    timer.fire()
    callback, argument = FakeMicroPython.scheduled.pop(0)
    callback(argument)
    assert safe_calls == ['fan_01', 'relay_01']
    assert home.offline_protection_active is True

    # 持续离线不得反复写执行器，避免每 100 ms 触发一次硬件输出。
    timer.fire()
    callback, argument = FakeMicroPython.scheduled.pop(0)
    callback(argument)
    assert safe_calls == ['fan_01', 'relay_01']
    assert home.offline_protection_active is True

    # 网络恢复后不自动回到失联前的输出；后续仅由本地规则或平台新指令控制。
    FakeNetwork.connected = True
    timer.fire()
    callback, argument = FakeMicroPython.scheduled.pop(0)
    callback(argument)
    assert safe_calls == ['fan_01', 'relay_01']
    assert home.offline_protection_active is False


# 该文件不依赖 pytest，直接在 MicroPython REPL 中运行即可执行全部行为测试。
test_set_command_runs_registered_action_and_acknowledges()
test_default_keepalive_detects_abnormal_program_stop_promptly()
test_non_set_message_does_not_run_endpoint_action()
test_motor_speed_action_drives_mpython_motor_and_reports_state()
test_reconnect_restores_command_subscription()
test_register_endpoint_announces_once_after_connect()
test_non_ascii_rule_payload_uses_utf8_bytes()
test_reconnect_reannounces_registered_endpoints()
test_reconnect_does_not_reenter_owner_restore_callbacks()
test_subscribe_commands_runs_check_msg_from_one_scheduled_timer_and_restores_after_reconnect()
test_command_poll_does_not_read_socket_during_publish()
test_local_rule_uses_registered_action_and_is_tick_by_existing_timer()
test_wifi_loss_runs_each_registered_safe_output_once_and_never_restores_it()
print('smart_home_mqtt MicroPython tests passed')
