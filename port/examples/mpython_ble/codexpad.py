from mpython_ble.application.codexpad import (
    CodexPad,
    BUTTON_CROSS_A,
    BUTTON_START,
)
import time


def show_input(buttons, axes):
    print("buttons=0x{:08X}, left=({}, {}), right=({}, {})".format(
        buttons, axes[0], axes[1], axes[2], axes[3]
    ))


pad = CodexPad()
pad.on_input(show_input)

# Connect either a C10 or S10.  For multiple nearby controllers, replace this
# with: pad.scan_and_connect(BUTTON_START | BUTTON_CROSS_A)
if not pad.connect():
    raise RuntimeError(pad.last_error)

while True:
    pad.poll()
    time.sleep_ms(20)
