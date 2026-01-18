from mpython import *
import audio
from audio import sr

audio.set_volume(100)
sr.init(wakeup_word='有什么吩咐',timeout=6000)
sr.clear()
sr.add(1,'kai deng')
sr.add(2,'guan deng')
sr.update()
while True:
    res = sr.get_latest_id()
    if res != 0:
        print('recognized command id:', res)
    if res == 1:
        print('open light')
    elif res == 2:
        print('close light')
