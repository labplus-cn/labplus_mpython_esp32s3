import audio
from audio import sr
from mpython import *

sr.init(wakeup_word='我在',timeout=6000)
import time 
time.sleep(10)
sr.add(1,'kai deng')
sr.add(2,'guan deng')
sr.update()
print('hello')

def on_button_a_pressed(_):
    sr.trigger_wakeup()
    # sr.start_vcmd_detection()


button_a.event_pressed = on_button_a_pressed

while True:
    res = sr.get_latest_id()
    if res == 1:
        print('opened')
    elif res == 2:
        print('closed')
    time.sleep(1)