'''
底层分别配置了音频输入设备和音频输出设备，但硬件上输入/输出接在同一IIS上，使用相同的时钟线，
因此同一时刻，输入/输出时钟是一致的，所以没有办法实现输入/输出不同采样率情况下同时工作，即同时
用不同采样率录音和播放。
解决办法：codec和IIS配置为一固定采样率和通道数及位宽，通过软件resample(libsamplerate 库)实现不同需求的采样率。
'''

from mpython import *

my_wifi = wifi()
my_wifi.connectWiFi("office", "wearemaker")
# my_wifi.connectWiFi("labplus_dev", "helloworld")

import audio

# import gc;gc.collect()
audio.player_init()
audio.set_volume(50)
audio.play('http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3')
# time.sleep(5)
# audio.stop()
# time.sleep_ms(50) # 需要一定时时间释放播放资源
# print("record start........")
# audio.recorder_init()
# audio.record("file://littlefs/1.opus", 5)
# print('record end.....')
# audio.recorder_deinit()

# audio.player_init()
# audio.set_volume(100)
# audio.play('file://littlefs/1.opus')