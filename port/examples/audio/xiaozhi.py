# # main.py -- put your code here!from mpython import
# from mpython import
# import xiaozhi

# def show_code(code, msg):
#     print("Activate at: https://xiaozhi.me  Code:", code)
#     display.clear(lcd.GREEN) # 清屏，背景设置为绿色
#     display.DispChar("请在小智官网添加设备，输入以下验证码", 0, 20, lcd.WHITE, True)
#     display.DispChar(code, 100, 110, lcd.WHITE, False)
#     display.show()
    
# my_wifi = wifi()
# # my_wifi.connectWiFi("labplus_dev", "helloworld")
# my_wifi.connectWiFi("office", "wearemaker")

# xiaozhi.activate("https://api.tenclass.net/xiaozhi/ota/", on_code=show_code)

# import audio
# # audio.init()   # 初始化音频硬件

# import xiaozhi
# xiaozhi.record_to_file("/rec.opus", 5000)   # 录音 5 秒

#  ── 首次使用：激活设备 ────────────────────────────────────────

import xiaozhi
from mpython import *
import audio

# 连接 WiFi
w = wifi()
w.connectWiFi("office", "wearemaker")

# 激活（获取验证码后在 xiaozhi.me 绑定设备）
def show_code(code, msg):
    print("验证码:", code)

xiaozhi.activate("https://api.tenclass.net/xiaozhi/ota/", on_code=show_code)
time.sleep(5)
# 激活成功后 url/token 自动写入 NVS，后续无需重复激活

#  ── 日常使用：语音会话 ────────────────────────────────────────

# import audio, xiaozhi
# from mpython import wifi

# w = wifi()
# w.connectWiFi("office", "wearemaker")

# audio.init()          # 初始化音频硬件（麦克风 + 扬声器）

# 回调（可选）
def on_state(state):
    names = ["idle", "connecting", "connected",
            "listening", "speaking", "error"]
    print("State:", names[state])

def on_stt(text):
    print("识别:", text)

def on_tts(state, text):
    print("TTS:", state, text or "")

def on_wakeup():
    print("is wakeup!")

# 初始化（url/token 自动从 NVS 读取）
xiaozhi.init()            # 可选: xiaozhi.init(volume=70)
xiaozhi.on_state(on_state)
xiaozhi.on_stt(on_stt)
xiaozhi.on_tts(on_tts)
xiaozhi.on_wakeup(on_wakeup)

# 启动会话（阻塞直到握手完成，然后进入 LISTENING 空闲状态）
xiaozhi.start()

# 主循环：派发回调 + 手动触发录音（按键模式示例）
# while True:
#     xiaozhi.poll()
#     if button_a.value() == 0:           # 按下 A 键开始说话
#         xiaozhi.listen_start()
#     elif button_b.value() == 0:         # 按下 B 键停止
#         xiaozhi.listen_stop()
#     time.sleep_ms(20)

# 中断 TTS 播放（切回空闲）
# xiaozhi.abort()

# 结束会话
# xiaozhi.stop()
# xiaozhi.deinit()