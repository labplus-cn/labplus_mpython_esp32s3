# 本文件会被顶层CMakeList.txt包含
# 可以在本文件定义一些板级源文件及目录
set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    ${SDKCONFIG_IDF_VERSION_SPECIFIC}
    boards/sdkconfig.ble 
    boards/sdkconfig.240mhz
    boards/labplus_Ledong_v2/sdkconfig.spiram 
    boards/labplus_Ledong_v2/sdkconfig.board
    # boards/labplus_Ledong_v2/sdkconfig.usb
)

if(NOT MPY_PORT_DIR)
    get_filename_component(MPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
endif()

if(NOT PROJECT_PATH)
    get_filename_component(PROJECT_PATH ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
endif()

set(MICROPY_SOURCE_BOARD
    ${MPY_PORT_DIR}/lib/decode_png/LP_lodepng.c
    ${MPY_PORT_DIR}/builtins/main.c
    ${MPY_PORT_DIR}/builtins/modmusictunes.c
    ${MPY_PORT_DIR}/builtins/modmusic.c
    ${MPY_PORT_DIR}/builtins/esp32_nvs.c
    ${MPY_PORT_DIR}/builtins/machine_pin.c
    ${MPY_PORT_DIR}/builtins/machine_touchpad.c
    ${MPY_PORT_DIR}/builtins/modframebuf.c
    ${MPY_PORT_DIR}/builtins/machine_i2c.c
)

set(MICROPY_SOURCE_BOARD_DIR
    ${MPY_PORT_DIR}/drivers
    ${MPY_PORT_DIR}/drivers/audio/include
    ${MPY_PORT_DIR}/lib
    ${MPY_PORT_DIR}/lib/decode_png
    ${MPY_PORT_DIR}/builtins
    ${MPY_PORT_DIR}/boards
)

set(LABPLUS_LEDONG_V2_BOARD ON)

list(APPEND EXTRA_COMPONENT_DIRS
    ${PROJECT_PATH}/esp-gmf/packages/esp_audio_render 
    ${PROJECT_PATH}/esp-gmf/packages/esp_audio_simple_player
    ${PROJECT_PATH}/esp-gmf/packages/esp_board_manager 
    ${PROJECT_PATH}/esp-gmf/packages/esp_capture
    ${PROJECT_PATH}/esp-gmf/packages/gmf_app_utils
    ${PROJECT_PATH}/esp-gmf/packages/gmf_loader
    ${PROJECT_PATH}/esp-gmf/gmf_core
    ${PROJECT_PATH}/esp-gmf/elements/gmf_ai_audio
    ${PROJECT_PATH}/esp-gmf/elements/gmf_audio
    ${PROJECT_PATH}/esp-gmf/elements/gmf_io
    ${PROJECT_PATH}/esp-gmf/elements/gmf_misc
)

list(APPEND EXTRA_COMPONENT_DIRS ${MPY_PORT_DIR}/usercmodule/esp-who/esp-who/components/esp-code-scanner)
list(APPEND EXTRA_COMPONENT_DIRS ${MPY_PORT_DIR}/usercmodule/esp-who/esp-who/components/esp-dl)
list(APPEND EXTRA_COMPONENT_DIRS ${MPY_PORT_DIR}/usercmodule/esp-who/esp-who/components/modules)
list(APPEND EXTRA_COMPONENT_DIRS ${MPY_PORT_DIR}/usercmodule/esp-who/esp-who/components/fb_gfx)
list(APPEND EXTRA_COMPONENT_DIRS ${MPY_PORT_DIR}/usercmodule/esp-who/esp-who/components/esp32-camera)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)
