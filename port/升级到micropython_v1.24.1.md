# MicroPython 移植与固件构建

> 文件名中的 `v1.24.1` 是历史升级记录名称，不代表当前依赖版本。以仓库中的子模块和版本宏为准：目前 `micropython/py/mpconfig.h` 的版本宏为 **1.26.1**，`esp-idf` 子模块及项目使用的构建环境为 **ESP-IDF 5.4.3**。不要再按旧记录切换到 MicroPython 1.23.0、ESP-IDF 5.0.4/5.2.5，或套用旧的 IDF 补丁。

## 项目结构与移植入口

仓库将 MicroPython 作为 ESP-IDF 项目构建，顶层项目目录是 `port/`，不是 `micropython/ports/esp32/`。主要位置如下：

| 路径 | 作用 |
| --- | --- |
| `micropython/` | MicroPython 子模块；`micropython/mpy-cross/` 提供冻结模块交叉编译器 |
| `esp-idf/`、`esp-gmf/` | 当前仓库的 ESP-IDF 与 ESP-GMF 子模块；旧文档中的 esp-adf 依赖描述已过时 |
| `port/CMakeLists.txt`、`port/main/`、`port/esp32_common.cmake` | IDF 项目入口、`main` 组件和通用源码/组件配置 |
| `port/boards/<板卡>/` | 板级 `mpconfigboard.cmake`、`sdkconfig.*`、分区表、冻结模块清单 |
| `port/modules/`、`port/boards/<板卡>/modules/` | 通用及板级冻结 Python 模块 |
| `port/usercmodule/micropython.cmake` | 当前统一引入 LVGL、LCD、音频等用户 C 模块；无需再手动传旧版 `USER_C_MODULES` 路径 |
| `port/file_system/`、`port/en/` | 默认 LittleFS 文件和英文覆盖资源 |
| `port/makeimg.py`、`port/filesystem_image.py` | LittleFS 打包、语言配置生成及完整固件合并 |

`port/CMakeLists.txt` 选择板卡和语言，包含板级 `mpconfigboard.cmake`，并按顺序合并 `SDKCONFIG_DEFAULTS`。板级 `manifest.py` 包含通用 `port/boards/manifest.py`，再冻结本板的 `modules/`。增加驱动时，先判断它属于通用模块、板级模块、文件系统脚本，还是 C 用户模块；不要为中英文复制整套驱动。

## 准备 WSL 构建环境

以下命令在 WSL 中执行；路径按本机仓库位置调整。每个新终端都要激活 IDF 环境：

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh
cd /mnt/g/labplus-project/labplus_mpython_esp32s3
idf.py --version
```

首次拉取仓库时，在确认子模块内没有需要保留的未提交修改后，初始化子模块：

```bash
git submodule update --init --recursive
make -C micropython/mpy-cross
cd port
```

`make -C micropython/mpy-cross` 从仓库根目录运行；后续 `idf.py` 命令从 `port/` 运行。构建中如出现组件锁文件变动，先核对 IDF 环境与依赖版本，不要直接覆盖已有改动。

## 构建板卡与语言版本

`MICROPY_LANGUAGE` 默认是 `zh`；目前只有 `labplus_Ledong_v2` 和 `mpython_v3` 开放 `en`。每个板卡/语言组合使用独立构建目录，且同一仓库中的不同组合应串行构建，避免共享的 `managed_components/` 与组件锁文件互相干扰。

```bash
idf.py -B build_ledong_v2_zh -D MICROPY_BOARD=labplus_Ledong_v2 -D MICROPY_LANGUAGE=zh build
idf.py -B build_ledong_v2_en -D MICROPY_BOARD=labplus_Ledong_v2 -D MICROPY_LANGUAGE=en build
idf.py -B build_mpython_v3_zh -D MICROPY_BOARD=mpython_v3 -D MICROPY_LANGUAGE=zh build
idf.py -B build_mpython_v3_en -D MICROPY_BOARD=mpython_v3 -D MICROPY_LANGUAGE=en build
```

其他板卡当前只能选择中文；例如：

```bash
idf.py -B build_xunfei_primary_zh -D MICROPY_BOARD=labplus_xunfei_js_primary -D MICROPY_LANGUAGE=zh build
```

项目的 `merge_bin` 已是默认构建目标，`idf.py ... build` 会在应用编译后自动打包 LittleFS 并合并固件。只需重新执行打包目标时，可从 `port/` 运行：

```bash
ninja -C build_mpython_v3_en merge_bin
```

构建目录中的交付物要区分：

- `<板卡>-<版本>-<提交>_<日期>-<语言>.bin`：项目生成的**完整镜像**，包含 bootloader、分区表、应用、LittleFS 和对应语音模型；中文版还按分区配置包含中文语音数据。
- 同名 `.uf2`：当前 `makeimg.py` 仅由 `micropython.bin`（应用）生成，**不是完整镜像**，不能替代首次完整烧录。
- `merged-binary.bin`：ESP-IDF 自带 `merge-bin` 的产物；不要与本项目的 `merge_bin` 目标和上述完整镜像混淆。

中英文资源覆盖顺序、英文 Logo、ESP-SR 模型及 K230 标签的细节见[中英文固件与资源分布](中英文固件与资源分布.md)。`MICROPY_LANGUAGE` 同时决定构建时的默认语言和打包生成的 `/lib/firmware_language.py`；K230 的 `set_voice_language()` 只临时切换识别结果文本，不会切换已编译的 UI 或语音模型。

## 烧录与串口

首次烧录或需要更新 LittleFS/语音模型时，选择与板卡、语言匹配的完整 `.bin`，确认实际文件名和串口后从地址 `0x0` 写入 ESP32-S3：

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x0 "build_mpython_v3_en/实际生成的固件文件.bin"
```

烧录完整镜像会覆盖固件包含的文件系统分区，先备份板上需要保留的用户文件。以上是文件名占位示例，不能原样复制执行。`idf.py flash` 按 IDF 的 `flash_project_args` 烧录相应分区；当前配置不包含项目生成的 LittleFS 镜像，因此不能代替完整镜像烧录。只查看串口输出可运行：

```bash
idf.py -B build_mpython_v3_en -p /dev/ttyACM0 monitor
```

WSL 下如需使用 Windows 连接的 USB 串口，先在 Windows PowerShell 用 `usbipd list` 确认 BUSID，再按需 `usbipd bind --busid <BUSID>` 和 `usbipd attach --wsl --busid <BUSID>`；回到 WSL 确认实际设备名，不能假定每次都是 `/dev/ttyACM0`。

## 移植维护要点

1. 新板卡应在 `port/boards/<板卡>/` 放置 `mpconfigboard.cmake`、板级 `sdkconfig.*`、分区表和 `manifest.py`，并明确冻结模块与外设源码。修改分区表后必须重新生成并烧录完整镜像。
2. C 模块注册、QSTR、板级源文件和组件依赖分别检查 `port/usercmodule/`、`port/qstrdefsport.h`、板级 `mpconfigboard.cmake` 与 `port/esp32_common.cmake`；不要直接复制旧版 MicroPython 或 IDF 文件覆盖当前实现。
3. 被长期持有的 MicroPython 对象或指针需纳入 GC 根管理。可参考现有 `modespnow.c` 中的 `MP_REGISTER_ROOT_POINTER(...espnow_singleton)` 和 `MP_STATE_PORT(espnow_singleton)` 用法；移植其他模块时应结合其实际生命周期处理。
4. UI/图片放入文件系统资源目录；英文开机 Logo 由 C 数组编入应用，不会因替换 LittleFS 中的 PNG 自动更新。TinyUSB、RMT 等组件兼容问题应先核对当前 IDF 与组件依赖，不要照旧记录直接修改 `managed_components/`。

本文件保留原有历史文件名以免破坏引用；后续升级以仓库内的版本宏、子模块提交和板级配置为准。
