"""CodexPad C10/S10 basic polling example.

This is the native ESP32-S3 version of the CodexPad upstream
``examples/basic_polling`` example.  The firmware driver scans for the
nearest ``CodexPad-`` device, so no Bluetooth address or aioble is required.
"""

import time

from mpython_ble.application import CodexPad
from mpython_ble.application.codexpad import (
    AXIS_LEFT_STICK_X,
    AXIS_LEFT_STICK_Y,
    AXIS_RIGHT_STICK_X,
    AXIS_RIGHT_STICK_Y,
    BUTTON_CIRCLE_B,
    BUTTON_CROSS_A,
    BUTTON_DOWN,
    BUTTON_HOME,
    BUTTON_LEFT,
    BUTTON_L1,
    BUTTON_L2,
    BUTTON_L3,
    BUTTON_R1,
    BUTTON_R2,
    BUTTON_R3,
    BUTTON_RIGHT,
    BUTTON_SELECT,
    BUTTON_SQUARE_X,
    BUTTON_START,
    BUTTON_TRIANGLE_Y,
    BUTTON_UP,
    TX_POWER_0_DBM,
)


_BUTTONS = (
    (BUTTON_UP, "Up"),
    (BUTTON_DOWN, "Down"),
    (BUTTON_LEFT, "Left"),
    (BUTTON_RIGHT, "Right"),
    (BUTTON_SQUARE_X, "Square(X)"),
    (BUTTON_TRIANGLE_Y, "Triangle(Y)"),
    (BUTTON_CROSS_A, "Cross(A)"),
    (BUTTON_CIRCLE_B, "Circle(B)"),
    (BUTTON_L1, "L1"),
    (BUTTON_L2, "L2"),
    (BUTTON_L3, "L3"),
    (BUTTON_R1, "R1"),
    (BUTTON_R2, "R2"),
    (BUTTON_R3, "R3"),
    (BUTTON_SELECT, "Select"),
    (BUTTON_START, "Start"),
    (BUTTON_HOME, "Home"),
)


def connect_until_ready(pad):
    while True:
        print("Scanning and connecting to CodexPad...")
        if pad.connect(timeout_ms=20000, scan_ms=5000):
            print("Remote device name:", pad.device_name)
            if not pad.set_remote_tx_power(TX_POWER_0_DBM):
                print("Remote TX power setting is unavailable")
            print("Connected")
            return
        print("Connection failed:", pad.last_error)
        time.sleep_ms(1000)


def print_input_snapshot(pad):
    button_text = []
    for button, name in _BUTTONS:
        button_text.append("{}:{}".format(name, int(pad.button_state(button))))
    print(" ".join(button_text))
    print(
        "L(X:{:3}, Y:{:3}) R(X:{:3}, Y:{:3})".format(
            pad.axis_value(AXIS_LEFT_STICK_X),
            pad.axis_value(AXIS_LEFT_STICK_Y),
            pad.axis_value(AXIS_RIGHT_STICK_X),
            pad.axis_value(AXIS_RIGHT_STICK_Y),
        )
    )


pad = CodexPad()
connect_until_ready(pad)
was_ready = True

while True:
    # Native BLE notifications are handled by the IRQ callback.  poll() only
    # services the driver's automatic reconnect state machine.
    pad.poll()
    if not pad.is_ready():
        if was_ready:
            print("Disconnected; waiting for automatic reconnect...")
        was_ready = False
        time.sleep_ms(20)
        continue

    if not was_ready:
        print("Reconnected:", pad.device_name)
    was_ready = True
    print_input_snapshot(pad)
    time.sleep_ms(100)
