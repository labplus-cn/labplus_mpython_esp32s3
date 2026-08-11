"""BLE Central driver shared by CodexPad-C10 and CodexPad-S10.

Both controllers use the same custom BLE GATT input protocol.  Input reports
from characteristic FFA1 have the following little-endian layout::

    uint32 button_mask, uint8 left_x, left_y, right_x, right_y

Only one object should own ``bluetooth.BLE().irq()`` at a time.
"""

import bluetooth
import struct
import time
from bluetooth import UUID

from ..const import IRQ


BUTTON_UP = 1 << 0
BUTTON_DOWN = 1 << 1
BUTTON_LEFT = 1 << 2
BUTTON_RIGHT = 1 << 3
BUTTON_SQUARE_X = 1 << 4
BUTTON_TRIANGLE_Y = 1 << 5
BUTTON_CROSS_A = 1 << 6
BUTTON_CIRCLE_B = 1 << 7
BUTTON_L1 = 1 << 8
BUTTON_L2 = 1 << 9
BUTTON_L3 = 1 << 10
BUTTON_R1 = 1 << 11
BUTTON_R2 = 1 << 12
BUTTON_R3 = 1 << 13
BUTTON_SELECT = 1 << 14
BUTTON_START = 1 << 15
BUTTON_HOME = 1 << 16

AXIS_LEFT_STICK_X = 0
AXIS_LEFT_STICK_Y = 1
AXIS_RIGHT_STICK_X = 2
AXIS_RIGHT_STICK_Y = 3
AXIS_CENTER = 0x80

TX_POWER_MINUS_16_DBM = -16
TX_POWER_MINUS_12_DBM = -12
TX_POWER_MINUS_8_DBM = -8
TX_POWER_MINUS_5_DBM = -5
TX_POWER_MINUS_3_DBM = -3
TX_POWER_MINUS_1_DBM = -1
TX_POWER_0_DBM = 0
TX_POWER_1_DBM = 1
TX_POWER_2_DBM = 2
TX_POWER_3_DBM = 3
TX_POWER_4_DBM = 4
TX_POWER_5_DBM = 5
TX_POWER_6_DBM = 6

_CODEXPAD_PREFIX = b"CodexPad-"
_MANUFACTURER_HEADER = b"CodexPad"
_INPUTS_SERVICE_UUID = UUID(0xFFA0)
_INPUTS_CHARACTERISTIC_UUID = UUID(0xFFA1)
_TX_POWER_SERVICE_UUID = UUID(0x1804)
_TX_POWER_CHARACTERISTIC_UUID = UUID(0x2A07)


def _ad_field(payload, field_type):
    """Return the first advertising field with ``field_type`` or ``None``."""
    index = 0
    while index + 1 < len(payload):
        field_length = payload[index]
        if field_length == 0:
            break
        field_end = index + field_length + 1
        if field_end > len(payload):
            break
        if payload[index + 1] == field_type:
            return bytes(payload[index + 2:field_end])
        index = field_end
    return None


def _device_name(payload):
    return _ad_field(payload, 0x09) or _ad_field(payload, 0x08)


def _advertised_button_mask(payload):
    """Decode CodexPad manufacturer data, returning ``None`` when absent."""
    data = _ad_field(payload, 0xFF)
    # AD manufacturer data begins with the 16-bit company identifier (FFFF).
    if data is None or len(data) < 2 + 16:
        return None
    data = data[2:]
    if data[:8] != _MANUFACTURER_HEADER:
        return None
    return struct.unpack_from("<I", data, 11)[0]


class CodexPad(object):
    """Input driver for CodexPad-C10 and CodexPad-S10 BLE controllers.

    ``connect()`` scans for either controller model.  To select a controller
    without knowing its address, call ``scan_and_connect(mask)`` while holding
    exactly the buttons in ``mask``.  Do not use ``BUTTON_HOME`` as the sole
    mask: holding Home powers the controller off.
    """

    def __init__(self, name_prefix=_CODEXPAD_PREFIX, ble=None, debug=False):
        if isinstance(name_prefix, str):
            name_prefix = name_prefix.encode()
        self.ble = ble if ble is not None else bluetooth.BLE()
        self.ble.active(True)
        self.ble.irq(self._irq)
        self.name_prefix = bytes(name_prefix)
        self.debug = debug
        self.connected_handle = None
        self.device_name = None
        self.last_error = None
        self.model = None
        self._state = "idle"
        self._candidate = None
        self._scan_entries = {}
        self._button_mask = None
        self._service_ranges = {}
        self._service_queue = []
        self._service_index = -1
        self._current_service = None
        self._input_value_handle = None
        self._tx_power_value_handle = None
        self._ready = False
        self._auto_reconnect = False
        self._retry_at = time.ticks_ms()
        self._input_callback = None
        self._previous_button_states = 0
        self._button_states = 0
        self._previous_axis_values = [
            AXIS_CENTER, AXIS_CENTER, AXIS_CENTER, AXIS_CENTER
        ]
        self._axis_values = [AXIS_CENTER, AXIS_CENTER, AXIS_CENTER, AXIS_CENTER]

    def on_input(self, callback):
        """Set ``callback(button_states, axis_values)`` for input updates."""
        self._input_callback = callback

    def connect(self, timeout_ms=20000, scan_ms=5000):
        """Scan and connect to the nearest CodexPad-C10 or CodexPad-S10."""
        return self._connect_with_mask(None, timeout_ms, scan_ms)

    def scan_and_connect(self, button_mask, timeout_ms=20000, scan_ms=5000):
        """Connect only when the advertising button state exactly matches mask."""
        if button_mask == BUTTON_HOME:
            raise ValueError("BUTTON_HOME alone cannot be used as a connection mask")
        return self._connect_with_mask(button_mask, timeout_ms, scan_ms)

    def _connect_with_mask(self, button_mask, timeout_ms, scan_ms):
        if self._ready:
            return True
        self._auto_reconnect = True
        self.last_error = None
        self._button_mask = button_mask
        self._start_scan(scan_ms)
        deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            if self._ready:
                return True
            if self._state == "idle" and self.last_error is not None:
                return False
            time.sleep_ms(20)
        self.last_error = "connection timed out"
        self._cancel_pending_operation()
        return False

    def poll(self):
        """Maintain automatic reconnection; call regularly from the main loop."""
        if (
            self._auto_reconnect
            and self._state == "idle"
            and time.ticks_diff(time.ticks_ms(), self._retry_at) >= 0
        ):
            self.last_error = None
            self._start_scan(5000)

    def disconnect(self):
        """Disconnect and turn off automatic reconnection."""
        self._auto_reconnect = False
        self._cancel_pending_operation()

    def is_connected(self):
        return self.connected_handle is not None

    def is_ready(self):
        return self._ready

    def button_state(self, button):
        return (self._button_states & button) != 0

    def pressed(self, button):
        return not (self._previous_button_states & button) and bool(
            self._button_states & button
        )

    def released(self, button):
        return bool(self._previous_button_states & button) and not (
            self._button_states & button
        )

    def holding(self, button):
        return bool(self._previous_button_states & button) and bool(
            self._button_states & button
        )

    @property
    def button_states(self):
        return self._button_states

    def axis_value(self, axis):
        return self._axis_values[axis]

    @property
    def axis_values(self):
        return tuple(self._axis_values)

    def has_axis_value_changed(self, axis, threshold=1):
        if axis not in (0, 1, 2, 3) or threshold < 0:
            return False
        previous = self._previous_axis_values[axis]
        current = self._axis_values[axis]
        return previous != current and (
            current == 0 or current == 255 or abs(current - previous) >= threshold
        )

    def set_remote_tx_power(self, tx_power, response=True):
        """Set the controller transmit power in dBm (-16, -12..+6)."""
        valid = (-16, -12, -8, -5, -3, -1, 0, 1, 2, 3, 4, 5, 6)
        if tx_power not in valid:
            raise ValueError("unsupported CodexPad transmit power")
        if self.connected_handle is None or self._tx_power_value_handle is None:
            return False
        self.ble.gattc_write(
            self.connected_handle,
            self._tx_power_value_handle,
            struct.pack("<b", tx_power),
            1 if response else 0,
        )
        return True

    def _reset_discovery(self):
        self._candidate = None
        self._scan_entries = {}
        self._service_ranges = {}
        self._service_queue = []
        self._service_index = -1
        self._current_service = None
        self._input_value_handle = None
        self._tx_power_value_handle = None
        self._ready = False

    def _start_scan(self, scan_ms):
        if self._state != "idle":
            return
        self._reset_discovery()
        self._state = "scanning"
        print("CodexPad: scanning for", self.name_prefix)
        self.ble.gap_scan(scan_ms, 30000, 30000, True)

    def _cancel_pending_operation(self):
        if self._state == "scanning":
            self.ble.gap_scan(None)
        elif self._state == "connecting":
            try:
                self.ble.gap_connect(None)
            except (OSError, TypeError):
                pass
        elif self.connected_handle is not None:
            self.ble.gap_disconnect(self.connected_handle)
        else:
            self._state = "idle"

    def _fail(self, message):
        self.last_error = message
        self._ready = False
        print("CodexPad:", message)
        if self.connected_handle is not None:
            self._state = "disconnecting"
            self.ble.gap_disconnect(self.connected_handle)
        else:
            self._state = "idle"

    def _next_service(self):
        self._service_index += 1
        if self._service_index >= len(self._service_queue):
            if self._input_value_handle is None:
                self._fail("inputs characteristic FFA1 not found")
                return
            self._state = "subscribing"
            # CodexPad uses the standard CCCD immediately after FFA1.
            self.ble.gattc_write(
                self.connected_handle, self._input_value_handle + 1, b"\x01\x00", 1
            )
            return
        self._current_service = self._service_queue[self._service_index]
        start_handle, end_handle = self._service_ranges[self._current_service]
        self._state = "discovering_characteristics"
        self.ble.gattc_discover_characteristics(
            self.connected_handle, start_handle, end_handle
        )

    def _save_characteristic(self, value_handle, uuid):
        uuid = UUID(uuid)
        if self._current_service == "inputs" and uuid == _INPUTS_CHARACTERISTIC_UUID:
            self._input_value_handle = value_handle
        elif self._current_service == "tx_power" and uuid == _TX_POWER_CHARACTERISTIC_UUID:
            self._tx_power_value_handle = value_handle

    def _parse_inputs(self, data):
        if len(data) != 8:
            return
        self._previous_button_states = self._button_states
        self._previous_axis_values[0] = self._axis_values[0]
        self._previous_axis_values[1] = self._axis_values[1]
        self._previous_axis_values[2] = self._axis_values[2]
        self._previous_axis_values[3] = self._axis_values[3]
        values = struct.unpack("<IBBBB", data)
        self._button_states = values[0]
        self._axis_values[0] = values[1]
        self._axis_values[1] = values[2]
        self._axis_values[2] = values[3]
        self._axis_values[3] = values[4]
        if self._input_callback is not None:
            self._input_callback(self._button_states, tuple(self._axis_values))

    def _irq(self, event, data):
        if self.debug:
            print("CodexPad IRQ:", event, data)

        if event == IRQ.IRQ_SCAN_RESULT:
            addr_type, addr, adv_type, rssi, adv_data = data
            if self._state != "scanning":
                return
            addr = bytes(addr)
            name = _device_name(adv_data)
            button_mask = _advertised_button_mask(adv_data)
            # A CodexPad name and its manufacturer data can arrive in separate
            # ADV_IND / SCAN_RSP events, so retain both fields per address.
            entry = self._scan_entries.get(addr)
            if entry is None:
                entry = [addr_type, None, None]
                self._scan_entries[addr] = entry
            if name is not None:
                entry[1] = name
            if button_mask is not None:
                entry[2] = button_mask
            if entry[1] is None or not entry[1].startswith(self.name_prefix):
                return
            if self._button_mask is not None and entry[2] != self._button_mask:
                return
            if self._candidate is None or rssi > self._candidate[3]:
                self._candidate = (entry[0], addr, entry[1], rssi)

        elif event == IRQ.IRQ_SCAN_DONE:
            if self._state != "scanning":
                return
            if self._candidate is None:
                self.last_error = "CodexPad not found"
                self._state = "idle"
                return
            addr_type, addr, name, rssi = self._candidate
            self.device_name = name.decode("utf-8", "ignore")
            self.model = self.device_name
            self._state = "connecting"
            print("CodexPad: found", self.device_name, "RSSI", rssi)
            self.ble.gap_connect(addr_type, addr)

        elif event == IRQ.IRQ_PERIPHERAL_CONNECT:
            conn_handle, addr_type, addr = data
            if self._state != "connecting":
                return
            self.connected_handle = conn_handle
            self._state = "discovering_services"
            print("CodexPad: connected, discovering GATT")
            self.ble.gattc_discover_services(conn_handle)

        elif event == IRQ.IRQ_PERIPHERAL_DISCONNECT:
            conn_handle, addr_type, addr = data
            if conn_handle != self.connected_handle:
                return
            was_ready = self._ready
            self.connected_handle = None
            self._ready = False
            self._state = "idle"
            self._retry_at = time.ticks_add(time.ticks_ms(), 1000)
            if was_ready:
                print("CodexPad: disconnected; reconnect scheduled")

        elif event == IRQ.IRQ_GATTC_SERVICE_RESULT:
            conn_handle, start_handle, end_handle, uuid = data
            if conn_handle != self.connected_handle:
                return
            uuid = UUID(uuid)
            if uuid == _INPUTS_SERVICE_UUID:
                self._service_ranges["inputs"] = (start_handle, end_handle)
            elif uuid == _TX_POWER_SERVICE_UUID:
                self._service_ranges["tx_power"] = (start_handle, end_handle)

        elif event == IRQ.IRQ_GATTC_SERVICE_DONE:
            conn_handle, status = data
            if conn_handle != self.connected_handle:
                return
            if status != 0:
                self._fail("service discovery failed: {}".format(status))
                return
            self._service_queue = []
            for service in ("inputs", "tx_power"):
                if service in self._service_ranges:
                    self._service_queue.append(service)
            self._service_index = -1
            self._next_service()

        elif event == IRQ.IRQ_GATTC_CHARACTERISTIC_RESULT:
            conn_handle, def_handle, value_handle, properties, uuid = data
            if conn_handle == self.connected_handle:
                self._save_characteristic(value_handle, uuid)

        elif event == IRQ.IRQ_GATTC_CHARACTERISTIC_DONE:
            conn_handle, status = data
            if conn_handle != self.connected_handle:
                return
            if status != 0:
                self._fail("characteristic discovery failed: {}".format(status))
                return
            self._next_service()

        elif event == IRQ.IRQ_GATTC_WRITE_DONE:
            conn_handle, value_handle, status = data
            if conn_handle != self.connected_handle or self._state != "subscribing":
                return
            if value_handle != self._input_value_handle + 1:
                return
            if status != 0:
                self._fail("could not enable input notifications: {}".format(status))
                return
            self._state = "ready"
            self._ready = True
            self.last_error = None
            print("CodexPad: ready, device={}".format(self.device_name))

        elif event == IRQ.IRQ_GATTC_NOTIFY:
            conn_handle, value_handle, notify_data = data
            if conn_handle == self.connected_handle and value_handle == self._input_value_handle:
                self._parse_inputs(bytes(notify_data))
