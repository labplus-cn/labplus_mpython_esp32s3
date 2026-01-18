from mpython import *
import audio
from audio import baidu_tts

my_wifi = wifi()
my_wifi.connectWiFi("labplus_dev", "helloworld")
audio.set_volume(100)

a_key = "02d5HuRLSYoECDqVzIhiMzL0"
s_key = "NJGwxkHDAHUndpwQOpUMwJfNZGpglatE"
tet = "你好华人民共和国"
baidu_tts.config(a_key, s_key, tet)
baidu_tts.run() 
# while True:
#     baidu_tts.run() 
#     time.sleep(3)

