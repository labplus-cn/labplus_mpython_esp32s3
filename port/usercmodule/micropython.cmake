# Add the lv_binding_micropython.
if(MPYTHON_PRO_BOARD OR LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/lv_binding_micropython/micropython.cmake)

# add lcd driver module
include(${CMAKE_CURRENT_LIST_DIR}/lcd/micropython.cmake)

# add esp-who module
include(${CMAKE_CURRENT_LIST_DIR}/esp-who/micropython.cmake)

# add tts module
include(${CMAKE_CURRENT_LIST_DIR}/tts/micropython.cmake)

# add wake up and speech command module
include(${CMAKE_CURRENT_LIST_DIR}/speechCommand/micropython.cmake)


if(LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/touchpad/micropython.cmake)
endif()
endif()