import xiaozhiAI
from mpython import *
import audio

# 连接WiFi
w = wifi()
w.connectWiFi("labplus_dev", "helloworld")

# 初始化 - 自动模式（默认）
xiaozhiAI.init()

def on_button_a_pressed(_):
    xiaozhiAI.listen_start()

button_a.event_pressed = on_button_a_pressed

def on_button_a_released(_):
    xiaozhiAI.listen_stop()

button_a.event_released = on_button_a_released

# 回调（可选）
def on_state(state):
    names = ["idle", "connecting", "connected",
            "listening", "speaking", "error"]
    print("State:", names[state])

def on_stt(text):
    print("识别:", text)

def on_tts(state, text):
    print("TTS:", state, text or "")
    if text:
        display.clear(lcd.GREEN) # 清屏，背景设置为绿色
        display.DispChar(text, 0, 34, lcd.WHITE, True)
        display.show()

# def on_wakeup():
#     print("is wakeup!")

# 初始化（url/token 自动从 NVS 读取）
xiaozhiAI.init()            # 可选: xiaozhiAI.init(volume=70)
xiaozhiAI.on_state(on_state)
xiaozhiAI.on_stt(on_stt)
xiaozhiAI.on_tts(on_tts)
# xiaozhiAI.on_wakeup(on_wakeup)

# 主循环
while True:
    xiaozhiAI.poll()
    time.sleep_ms(20)