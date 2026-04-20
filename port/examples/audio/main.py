# main.py -- put your code here!from mpython import *
# from mpython import *
# import network
# import audio

# # my_wifi = wifi()
# # # my_wifi.connectWiFi("labplus_dev", "helloworld")
# # my_wifi.connectWiFi("office", "wearemaker")

# audio.player_init()
# # audio.play("https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3")
# audio.play("http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3")
# audio.play("file://littlefs/test.mp3")
# audio.play("file://littlefs/test.wav")

# from mpython import *
# import network
# my_wifi = wifi()
# my_wifi.connectWiFi("office", "wearemaker")
# import audio
# # A键暂停
# def on_button_a_pressed(_):
#     global res
#     audio.pause()
# button_a.event_pressed = on_button_a_pressed
# # B键继续
# def on_button_b_pressed(_):
#     global res
#     audio.resume()
# button_b.event_pressed = on_button_b_pressed
# audio.player_init(i2c)
# audio.set_volume(50)
# audio.play('http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3')
# # time.sleep(15)
# # audio.stop()

from mpython import *
import audio
def on_button_a_pressed(_):
    audio.pause()
button_a.event_pressed = on_button_a_pressed
def on_button_b_pressed(_):
    audio.resume()
button_b.event_pressed = on_button_b_pressed
audio.player_init(i2c)
audio.set_volume(50)
audio.play('test.mp3')