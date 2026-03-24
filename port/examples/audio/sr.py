import audio
import sc
from mpython import *

sc.init(wakeup_word='我在',timeout=6000)
import time 
# time.sleep(10)
sc.add(1,'kai deng')
sc.add(2,'guan deng')
sc.update()

def on_button_a_pressed(_):
    sc.trigger_wakeup()


button_a.event_pressed = on_button_a_pressed

while True:
    res = sc.get_latest_id()
    if res == 1:
        print('opened')
    elif res == 2:
        print('closed')
    time.sleep(1)