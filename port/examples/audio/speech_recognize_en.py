import audio
from audio import sr
from mpython import *
import time

# In English firmware, WakeNet uses "Hi ESP" wake word.
# Initialize audio player for prompt tone
audio.player_init()
audio.set_volume(80)

# Wakeup timeout is set to 6000ms.
sr.init(timeout=6000)

sr.add(1, 'turn on the light')
sr.add(2, 'turn off the light')
sr.add(3, 'play music')
sr.add(4, 'stop music')
sr.update()
print('Speech recognition ready! Please say: "Hi, ESP"')

def on_button_a_pressed(_):
    sr.trigger_wakeup()

button_a.event_pressed = on_button_a_pressed

last_woken = False

while True:
    is_woken = bool(sr.get_wakeup_flag())
    if is_woken and not last_woken:
        print("[Woken Up] 'Hi, ESP' detected! Playing 'I'm here'...")
        audio.play('im_here_16k.mp3')
    last_woken = is_woken

    res = sr.get_latest_id()
    if res == 1:
        print('--> Command 1 detected: turn on the light')
    elif res == 2:
        print('--> Command 2 detected: turn off the light')
    elif res == 3:
        print('--> Command 3 detected: play music')
    elif res == 4:
        print('--> Command 4 detected: stop music')
    time.sleep_ms(100)
