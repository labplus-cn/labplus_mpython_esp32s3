from mpython import oled

oled.fill(0)
oled.DispChar(" \u56fa\u4ef6\u65e5\u671f: 2025-4-25", 0, 13, 1)
oled.DispChar(" \u6587\u4ef6\u7cfb\u7edf: 2025-4-25", 0, 28, 1)
oled.DispChar(" 甘肃学镜掌控 ", 30, 42, 1)
oled.show()