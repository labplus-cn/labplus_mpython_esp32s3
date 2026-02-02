# main.py -- put your code here!from mpython import *
from mpython import *
import network
import audio

my_wifi = wifi()
# my_wifi.connectWiFi("labplus_dev", "helloworld")
my_wifi.connectWiFi("office", "wearemaker")

# def cb(state):
#     print("Play state:", state)
    
# p = audio.player(cb)
# p.set_vol(100)
# # p.play("http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3")
# # # p.play("https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3")
# # p.play("file://sd/test.amr")

# def rcb():
#     print("Record end.")
# r = audio.recorder()
# r.start("file://sd/1.wav", format=r.WAV, maxtime=5, endcb = rcb)
# # time.sleep(10)
# # print("stop rec")
# # p.play("file://sd/1.wav")

audio.player_init()
audio.play("https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3")
audio.play("http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3")