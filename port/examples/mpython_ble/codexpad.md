# CodexPad C10 / S10 蓝牙手柄

`CodexPad-C10` 和 `CodexPad-S10` 均可由本固件中的同一个 `CodexPad` 类连接。
ESP32-S3 作为 BLE 主机（Central）扫描、连接手柄，并接收实时按键和摇杆通知；不依赖经典蓝牙 HID，也不需要额外安装 `aioble`。

## 导入

```python
from mpython_ble.application import CodexPad
from mpython_ble.application.codexpad import (
    BUTTON_CROSS_A,
    BUTTON_START,
    BUTTON_UP,
    AXIS_LEFT_STICK_X,
)
```

> 同一时刻只能有一个对象使用 `bluetooth.BLE().irq()`。因此不能让 `CodexPad` 与其他直接操作 BLE IRQ 的驱动同时运行。

## 最小示例

```python
from mpython_ble.application import CodexPad
import time


def on_input(buttons, axes):
    print("buttons=0x{:08X}".format(buttons))
    print("left=({}, {}), right=({}, {})".format(
        axes[0], axes[1], axes[2], axes[3]
    ))


pad = CodexPad()
pad.on_input(on_input)

if not pad.connect():
    raise RuntimeError(pad.last_error)

while True:
    # 处理手柄断开后的自动重连。
    pad.poll()
    time.sleep_ms(20)
```

连接成功会打印类似信息：

```text
CodexPad: found CodexPad-S10 RSSI -55
CodexPad: connected, discovering GATT
CodexPad: ready, device=CodexPad-S10
```

完整可运行示例位于固件源码的 `port/examples/mpython_ble/codexpad.py`。

## 用按键组合选择手柄

附近有多个 CodexPad 时，可在扫描期间按住指定按键组合，只有广播按键状态**恰好**匹配时才连接：

```python
from mpython_ble.application import CodexPad
from mpython_ble.application.codexpad import BUTTON_START, BUTTON_CROSS_A

pad = CodexPad()

# 启动程序后，在目标手柄上同时按住 Start + A(Cross)。
if not pad.scan_and_connect(BUTTON_START | BUTTON_CROSS_A):
    raise RuntimeError(pad.last_error)
```

不要把 `BUTTON_HOME` 单独作为连接组合。长按 Home 会使手柄关机或重启，导致连接中断。

## 按键与摇杆

手柄每次输入变化会发送 8 字节通知：4 字节小端按键掩码，随后是左摇杆 X/Y、右摇杆 X/Y 四个 `0..255` 的值；`128` 为摇杆居中。

| 分组 | 常量 |
| --- | --- |
| 方向键 | `BUTTON_UP`、`BUTTON_DOWN`、`BUTTON_LEFT`、`BUTTON_RIGHT` |
| 面键 | `BUTTON_SQUARE_X`、`BUTTON_TRIANGLE_Y`、`BUTTON_CROSS_A`、`BUTTON_CIRCLE_B` |
| 肩键 / 摇杆按键 | `BUTTON_L1`、`BUTTON_L2`、`BUTTON_L3`、`BUTTON_R1`、`BUTTON_R2`、`BUTTON_R3` |
| 系统键 | `BUTTON_SELECT`、`BUTTON_START`、`BUTTON_HOME` |
| 摇杆轴 | `AXIS_LEFT_STICK_X`、`AXIS_LEFT_STICK_Y`、`AXIS_RIGHT_STICK_X`、`AXIS_RIGHT_STICK_Y` |

回调参数 `buttons` 是全部按键状态；`axes` 是顺序为 `(left_x, left_y, right_x, right_y)` 的元组。

也可以在循环中读取状态：

```python
if pad.pressed(BUTTON_CROSS_A):
    print("A 刚被按下")

if pad.holding(BUTTON_UP):
    print("正在按住上方向")

if pad.released(BUTTON_START):
    print("Start 刚被松开")

left_x = pad.axis_value(AXIS_LEFT_STICK_X)
all_buttons = pad.button_states
all_axes = pad.axis_values
```

`pressed()`、`released()` 和 `holding()` 均基于最近两次通知包比较。若希望每个输入包都处理，应优先使用 `on_input()` 回调。

## 设置手柄蓝牙发射功率

连接完成后可以设置手柄端发射功率，以平衡距离与耗电：

```python
from mpython_ble.application.codexpad import TX_POWER_0_DBM

if not pad.set_remote_tx_power(TX_POWER_0_DBM):
    print("当前手柄不支持发射功率设置，或尚未连接")
```

可用值为 `TX_POWER_MINUS_16_DBM`、`TX_POWER_MINUS_12_DBM`、`TX_POWER_MINUS_8_DBM`、`TX_POWER_MINUS_5_DBM`、`TX_POWER_MINUS_3_DBM`、`TX_POWER_MINUS_1_DBM`、`TX_POWER_0_DBM` 至 `TX_POWER_6_DBM`。

## 排查

- `CodexPad not found`：确认手柄已开启且在蓝灯闪烁的可发现状态；使用按键组合连接时，按键必须从扫描开始持续按住。
- 已连接但没有输入：确认程序注册了 `on_input()`，并且没有其他代码重新设置 BLE 的 IRQ 回调。
- 频繁断开：在主循环中调用 `pad.poll()`；检查电池电量，并让手柄和开发板保持较近距离。
- 不能连接 KS54 等普通手机游戏手柄：本驱动只支持 CodexPad C10/S10 的自定义 BLE 协议，不支持经典蓝牙 HID。

## 协议说明

该驱动扫描名称以 `CodexPad-` 开头的设备，发现输入服务 `0xFFA0` 与通知特征 `0xFFA1` 后订阅其 CCCD。C10 与 S10 采用相同的输入协议，因此无需选择不同的类。

官方参考：<https://gitee.com/CodexPad/codex_pad_guide/blob/main/connection_guide_native_ble.zh-CN.md>、<https://gitee.com/CodexPad/codex_pad_mpy_lib/blob/main/README.zh-CN.md>。
