"""CodexPad C10/S10 button-mask scan-and-connect example.

This is the native ESP32-S3 version of the upstream
``examples/scan_and_connect`` example.  Hold the selected buttons while the
controller is advertising; the driver connects only when the advertised mask
matches exactly.
"""

import time

from mpython_ble.application import CodexPad
from mpython_ble.application.codexpad import (
    AXIS_LEFT_STICK_X,
    AXIS_LEFT_STICK_Y,
    AXIS_RIGHT_STICK_X,
    AXIS_RIGHT_STICK_Y,
    BUTTON_CROSS_A,
    BUTTON_START,
    TX_POWER_0_DBM,
)


# Hold Start + A/Cross while the controller's blue light is flashing.
# For another combination, OR additional BUTTON_* constants into this mask.
BUTTON_MASK = BUTTON_START | BUTTON_CROSS_A


def connect_until_ready(pad):
    while True:
        print("Scanning with button mask 0x{:08X}".format(BUTTON_MASK))
        if pad.scan_and_connect(
            BUTTON_MASK,
            timeout_ms=10000,
            scan_ms=1000,
        ):
            print("Remote device name:", pad.device_name)
            if not pad.set_remote_tx_power(TX_POWER_0_DBM):
                print("Remote TX power setting is unavailable")
            print("Connected")
            return
        print("Scan/connect failed:", pad.last_error)
        time.sleep_ms(1000)


pad = CodexPad()
connect_until_ready(pad)
was_ready = True

while True:
    pad.poll()
    if not pad.is_ready():
        if was_ready:
            print("Disconnected; waiting for automatic masked reconnect...")
        was_ready = False
        time.sleep_ms(20)
        continue

    if not was_ready:
        print("Reconnected:", pad.device_name)
    was_ready = True

    if pad.pressed(BUTTON_CROSS_A):
        print("A/Cross pressed")
    if pad.released(BUTTON_CROSS_A):
        print("A/Cross released")
    if pad.pressed(BUTTON_START):
        print("Start pressed")
    if pad.released(BUTTON_START):
        print("Start released")

    axis_changed = (
        pad.has_axis_value_changed(AXIS_LEFT_STICK_X, 2),
        pad.has_axis_value_changed(AXIS_LEFT_STICK_Y, 2),
        pad.has_axis_value_changed(AXIS_RIGHT_STICK_X, 2),
        pad.has_axis_value_changed(AXIS_RIGHT_STICK_Y, 2),
    )
    if any(axis_changed):
        print(
            "L(X:{:3}, Y:{:3}) R(X:{:3}, Y:{:3})".format(
                pad.axis_value(AXIS_LEFT_STICK_X),
                pad.axis_value(AXIS_LEFT_STICK_Y),
                pad.axis_value(AXIS_RIGHT_STICK_X),
                pad.axis_value(AXIS_RIGHT_STICK_Y),
            )
        )

    time.sleep_ms(10)
