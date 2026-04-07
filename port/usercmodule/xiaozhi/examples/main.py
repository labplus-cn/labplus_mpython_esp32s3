import xiaozhiAI
from mpython import *
import audio

# 连接WiFi
w = wifi()
w.connectWiFi("labplus_dev", "helloworld")

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
    """
    语音识别(STT)回调函数
    
    当小智AI完成语音识别处理时触发此回调函数
    
    Args:
        text (str): 识别出的文本内容
    """
    # 打印识别结果到控制台
    print("识别:", text)
    if text:
        # 清屏并将背景设置为绿色
        display.clear(lcd.GREEN)
        display.DispChar("识别：" + text, 0, 0, lcd.WHITE, True)
        display.show()

def on_tts(state, text):
    """
    文本转语音(TTS)回调函数
    
    当小智AI完成文本转语音处理时触发此回调函数
    
    Args:
        state (int): TTS状态码
        text (str): 要转换为语音的文本内容，可能为空字符串
    """
    print("TTS:", state, text or "")
    
    # 如果有文本内容，则在显示屏上显示
    if text:
        display.clear(lcd.GREEN)
        display.DispChar(text, 0, 34, lcd.WHITE, True)
        display.show()

def on_wakeup():
    """
    唤醒功能回调函数
    
    当小智AI检测到唤醒词时触发此回调函数
    用于处理设备被语音唤醒时的逻辑
    
    注意：此函数当前被注释，如需使用唤醒功能请取消注释
    """
    # 打印唤醒状态到控制台
    print("is wakeup!")

try:
    xiaozhiAI.init()
    # 初始化（url/token 自动从 NVS 读取）
    xiaozhiAI.on_state(on_state)
    xiaozhiAI.on_stt(on_stt)
    xiaozhiAI.on_tts(on_tts)
    xiaozhiAI.on_wakeup(on_wakeup)

    # 主循环
    while True:
        xiaozhiAI.poll()
        time.sleep_ms(20)
except Exception as e:
    print(f"Session Error: {e}")
    xiaozhi.deinit()