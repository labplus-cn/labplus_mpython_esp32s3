# 本文件会被顶层CMakeList.txt包含
# 可以在本文件定义一些板级源文件及目录
set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    ${SDKCONFIG_IDF_VERSION_SPECIFIC}
    boards/sdkconfig.ble 
    boards/sdkconfig.240mhz
    boards/mpython_v3/sdkconfig.spiram 
    boards/mpython_v3/sdkconfig.board
    # boards/mpython_v3/sdkconfig.usb
)

if(NOT MPY_PORT_DIR)
    get_filename_component(MPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
endif()

set(MICROPY_SOURCE_BOARD
    ${MPY_PORT_DIR}/lib/decode_png/LP_lodepng.c
    ${MPY_PORT_DIR}/builtins/main.c
    ${MPY_PORT_DIR}/builtins/machine_timer.c
    ${MPY_PORT_DIR}/builtins/modmusictunes.c
    ${MPY_PORT_DIR}/builtins/modmusic.c
    ${MPY_PORT_DIR}/builtins/esp32_nvs.c
    ${MPY_PORT_DIR}/builtins/machine_pin.c
    ${MPY_PORT_DIR}/builtins/machine_touchpad.c
    ${MPY_PORT_DIR}/builtins/modframebuf.c
    ${MPY_PORT_DIR}/builtins/machine_i2c.c
    ${MPY_PORT_DIR}/builtins/machine_i2c_extmod.c
    ${MPY_PORT_DIR}/builtins/network_wlan.c
)

set(MICROPY_SOURCE_BOARD_DIR
    ${MPY_PORT_DIR}/drivers
    ${MPY_PORT_DIR}/lib/decode_png
    ${MPY_PORT_DIR}/builtins
    ${MPY_PORT_DIR}/boards
)

set(MPYTHON_V3_BOARD ON)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)
