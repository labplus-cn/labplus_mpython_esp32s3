# activate_demo.py
# 小智 AI 设备激活示例
#
# 首次使用前需要激活设备，将设备绑定到 xiaozhi.me 账号。
# 激活成功后，WebSocket 配置（url/token）自动写入 NVS，后续无需重复激活。
#
# 使用步骤：
#   1. 修改下方 WiFi 的 ssid 和 password
#   2. 运行本脚本，等待打印验证码
#   3. 打开 https://xiaozhi.me，登录后添加设备，输入验证码完成绑定

import xiaozhiAI
from mpython import *

# ── 1. 连接 WiFi ──────────────────────────────────────────────
w = wifi()
w.connectWiFi("office", "wearemaker")

# ── 2. 显示激活码回调 ─────────────────────────────────────────
def on_code(code, msg):
    print("请前往 https://xiaozhi.me 添加设备，输入验证码:", code)
    print("提示:", msg)
    # 如有屏幕，可在此显示验证码，例如：
    display.clear(lcd.WHITE)
    display.DispChar("验证码:", 0, 40, lcd.BLACK, True)
    display.DispChar(code, 60, 80, lcd.RED, False)
    display.show()

# ── 3. 执行激活（同步阻塞，最多约 30 秒）────────────────────
print("开始激活...")
xiaozhiAI.activate("https://api.tenclass.net/xiaozhi/ota/", on_code=on_code)

# ── 4. 验证激活结果 ───────────────────────────────────────────
cfg = xiaozhiAI.load_config()
if cfg:
    print("激活成功！")
    print("WebSocket URL:", cfg["url"])
else:
    print("激活未完成，请重新运行本脚本。")


import xiaozhiAI
from mpython import *

w = wifi()
w.connectWiFi("office", "wearemaker")

def on_code(code, msg):
    print(code)
    print(msg)
    
xiaozhiAI.activate("https://api.tenclass.net/xiaozhi/ota/", on_code=on_code)
