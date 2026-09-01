"""ESP32S3 到真实 MQTT Broker 的智能家居链路测试。

本脚本必须在掌控板的 MicroPython 环境运行，不在电脑 CPython 中运行。
测试范围：Wi-Fi 连接、MQTT 认证、智能家居 Topic 订阅、state 发布和订阅回环。
本脚本不连接 Aqara，也不执行 Aqara 云端 API 调用。

为了避免把密码提交到仓库，可以在板上放置一个未提交的
``smart_home_mqtt_broker_test_config.py``，例如：

    WIFI_SSID = '你的 Wi-Fi 名称'
    WIFI_PASSWORD = '你的 Wi-Fi 密码'
    BROKER_HOST = '192.168.1.149'
    BROKER_PORT = 1883
    CLIENT_ID = '平台已登记的 clientId'
    MQTT_USER = '平台已登记的 authenticationId'
    MQTT_PASSWORD = '平台已登记的 authenticationPwd'
    TOPIC_KEY = '平台 Topic.topic 字符串'
    ENDPOINT_ID = 'controller_01'

也可以不创建配置文件，直接在串口运行时按提示输入必填项。
"""

import network
import time
import ujson

from smart_home_mqtt import SmartHomeMQTT


try:
    import smart_home_mqtt_broker_test_config as _config
except ImportError:
    _config = None


def _configured(name, default=''):
    """读取板端私有配置；仓库中不保存任何真实账号密码。"""
    if _config is not None and hasattr(_config, name):
        value = getattr(_config, name)
        if value is not None:
            return value
    return default


def _required(name, prompt):
    """配置为空时通过 MicroPython REPL 交互输入。"""
    value = _configured(name)
    if value:
        return value
    value = input(prompt)
    if not value:
        raise ValueError(name + ' 不能为空')
    return value


def connect_wifi(ssid, password, timeout_ms=20000):
    """连接指定 Wi-Fi，并返回已联网的 STA 接口。"""
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        wlan.connect(ssid, password)
        deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
        while not wlan.isconnected():
            if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
                raise RuntimeError('Wi-Fi 连接超时')
            time.sleep_ms(250)
    return wlan


def _same_topic(topic, expected):
    """兼容 umqtt 回调返回 bytes 或 str 两种形式。"""
    if topic == expected:
        return True
    if isinstance(topic, bytes):
        return topic == expected.encode('utf-8')
    return False


def run():
    """执行一次真实 Broker 发布/订阅回环测试。"""
    ssid = _required('WIFI_SSID', 'WIFI SSID: ')
    wifi_password = _required('WIFI_PASSWORD', 'WIFI password: ')
    # Broker 地址随现场网段变化，不能依赖开发机旧地址；未配置时在板端询问。
    broker_host = _required('BROKER_HOST', 'MQTT Broker host/IP: ')
    broker_port = int(_configured('BROKER_PORT', 1883))
    client_id = _required('CLIENT_ID', 'MQTT clientId: ')
    mqtt_user = _required('MQTT_USER', 'MQTT authenticationId: ')
    mqtt_password = _required('MQTT_PASSWORD', 'MQTT authenticationPwd: ')
    topic_key = _required('TOPIC_KEY', 'Topic.topic / topic_key: ')
    endpoint_id = _configured('ENDPOINT_ID', 'controller_01')

    wlan = connect_wifi(ssid, wifi_password)
    print('[1/4] Wi-Fi connected, IP=' + wlan.ifconfig()[0])

    home = SmartHomeMQTT(
        server=broker_host,
        port=broker_port,
        client_id=client_id,
        user=mqtt_user,
        password=mqtt_password,
        topic_key=topic_key,
    )
    received = []

    def capture_message(topic, message):
        # 只记录本次 state 回环；不读取 umqtt 的私有回调属性。
        try:
            if _same_topic(topic, home.topic):
                envelope = ujson.loads(message)
                data = envelope.get('data')
                if (
                    envelope.get('msg_type') == 'smart_home'
                    and envelope.get('channel') == 'state'
                    and isinstance(data, dict)
                    and data.get('probe') == probe_id
                ):
                    received.append(envelope)
        except Exception:
            # 回环探测只关心自己的合法消息，不让解析异常中断 MQTT 接收循环。
            pass

    home.connect()
    print('[2/4] MQTT connected: ' + broker_host + ':' + str(broker_port))
    home.subscribe_commands()
    home.client.set_callback(capture_message)
    print('[3/4] subscribed: ' + home.topic)

    probe_id = 'esp32_probe_' + str(time.ticks_ms())
    home.publish_state(endpoint_id, {'online': True, 'probe': probe_id})
    print('[4/4] published state, qos=1, retain=false')

    deadline = time.ticks_add(time.ticks_ms(), 10000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        home.check_msg()
        if received:
            print('REAL MQTT BROKER TEST PASSED')
            print('received state probe: ' + probe_id)
            home.disconnect()
            return True
        time.sleep_ms(100)

    home.disconnect()
    raise RuntimeError('已发布但 10 秒内未收到订阅回环消息')


run()
