# Add the lv_binding_micropython.
include(${CMAKE_CURRENT_LIST_DIR}/lv_binding_micropython/micropython.cmake)

if(LABPLUS_LEDONG_V2_BOARD OR LABPLUS_XUNFEI_JS_PRIMARY_BOARD)
# add esp-who module
include(${CMAKE_CURRENT_LIST_DIR}/esp-who/micropython.cmake)
endif()

# add lcd module
include(${CMAKE_CURRENT_LIST_DIR}/tft_lcd/micropython.cmake)

# add lv displayer driver module
include(${CMAKE_CURRENT_LIST_DIR}/lv_displayer/micropython.cmake)

# add rfid module
include(${CMAKE_CURRENT_LIST_DIR}/rfid/micropython.cmake)

if(LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/touchpad/micropython.cmake)
endif()

# add audio module
include(${CMAKE_CURRENT_LIST_DIR}/audio/micropython.cmake)

# add xiaozhi AI voice module
include(${CMAKE_CURRENT_LIST_DIR}/xiaozhi/micropython.cmake)
