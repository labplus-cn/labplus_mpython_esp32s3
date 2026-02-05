import audio
from audio import sr

sr.init(wakeup_word='我在',timeout=6000)
import time 
time.sleep(10)
sr.add(1,'kai deng')
sr.add(2,'guan deng')
sr.update()
print('hello')
# while True:
#     res = sr.get_latest_id()
#     if res == 1:
#         print('开灯')
#     elif res == 2:
#         print('关灯')