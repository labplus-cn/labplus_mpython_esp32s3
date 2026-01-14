# main.py -- put your code here!from mpython import *
from mpython import *
import network
import audio

my_wifi = wifi()
my_wifi.connectWiFi("labplus_dev", "helloworld")
# my_wifi.connectWiFi("office", "wearemaker")

def cb(state):
    print("Play state:", state)
    
p = audio.player(cb)
p.set_vol(90)
# p.play("http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3")
# # p.play("https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3")
p.play("file://sd/test.amr")