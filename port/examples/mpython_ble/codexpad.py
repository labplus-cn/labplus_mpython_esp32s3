from mpython_ble.application.codexpad import (
    CodexPad,
    BUTTON_CROSS_A,
    BUTTON_START,
    TX_POWER_0_DBM,
)
import time


# Set a human-readable BLE address to select one controller, or leave empty
# to connect to the strongest CodexPad advertisement.
CODEXPAD_MAC = ""

# Set to True to use the CodexPad button-mask scan workflow when no MAC is set.
USE_BUTTON_MASK = False


def show_input(buttons, axes):
    print("buttons=0x{:08X}, left=({}, {}), right=({}, {})".format(
        buttons, axes[0], axes[1], axes[2], axes[3]
    ))


pad = CodexPad()
pad.on_input(show_input)

# Connect either a C10 or S10.  Set USE_BUTTON_MASK to True to select a target
# by holding Start + A/Cross while the controller is advertising.
if CODEXPAD_MAC:
    connected = pad.connect(CODEXPAD_MAC)
elif USE_BUTTON_MASK:
    connected = pad.scan_and_connect(BUTTON_START | BUTTON_CROSS_A)
else:
    connected = pad.connect()
if not connected:
    raise RuntimeError(pad.last_error)

if not pad.set_remote_tx_power(TX_POWER_0_DBM):
    print("Remote TX power setting is unavailable")

while True:
    pad.poll()
    time.sleep_ms(20)
