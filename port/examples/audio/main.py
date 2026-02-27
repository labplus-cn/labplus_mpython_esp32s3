# main.py -- put your code here!from mpython import *
from mpython import *
import network
import audio

# my_wifi = wifi()
# # my_wifi.connectWiFi("labplus_dev", "helloworld")
# my_wifi.connectWiFi("office", "wearemaker")

audio.player_init()
# audio.play("https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3")
audio.play("http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3")
audio.play("file://littlefs/test.mp3")
audio.play("file://littlefs/test.wav")