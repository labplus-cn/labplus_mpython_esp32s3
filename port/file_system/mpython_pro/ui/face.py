import machine
import AIcamera
from mpython import button_a

isDetect = False

def cb(_):
    global isDetect
    isDetect = True

AIcamera.init(AIcamera.FACE_DETECTION,cb)

def on_button_a_pressed(_):
    machine.reset()

button_a.event_pressed = on_button_a_pressed