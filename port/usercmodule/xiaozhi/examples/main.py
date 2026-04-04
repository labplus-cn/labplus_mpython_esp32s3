import xiaozhiAI
from mpython import *
import audio

# 连接WiFi
w = wifi()
w.connectWiFi("@Ruijie-sF325", "jiang1973@")

# 初始化 - 自动模式（默认）
xiaozhiAI.init()

# 主循环
while True:
    xiaozhiAI.poll()
    if button_a.value() == 0:
        xiaozhiAI.listen_start()
    time.sleep_ms(20)