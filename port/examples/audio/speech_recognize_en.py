import audio
from audio import sr
from mpython import *
import time

# In English firmware:
# 1. Wake words supported simultaneously: "Hi, ESP" and "Alexa".
# 2. sr.init() automatically plays "im_here_16k.mp3" when woken up (no extra user code needed!).
# 3. Timeout is set to 6000ms.
sr.init(timeout=6000)

# Recommended English commands (smooth acoustics, highest accuracy):
sr.add(1, 'turn on')
sr.add(2, 'turn off')
sr.add(3, 'play')
sr.add(4, 'stop')
sr.update()

print('Speech recognition ready! Please say: "Hi, ESP" or "Alexa"')

def on_button_a_pressed(_):
    # Press Button A to trigger wakeup manually (also auto-plays prompt sound!)
    sr.trigger_wakeup()

button_a.event_pressed = on_button_a_pressed

while True:
    cmd = sr.get_latest_id()
    if cmd == 1:
        print('--> Command 1: turn on')
        rgb.fill((255, 0, 0))
        rgb.write()
    elif cmd == 2:
        print('--> Command 2: turn off')
        rgb.fill((0, 0, 0))
        rgb.write()
    elif cmd == 3:
        print('--> Command 3: play')
    elif cmd == 4:
        print('--> Command 4: stop')
    time.sleep_ms(100)
