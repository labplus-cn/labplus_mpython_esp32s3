# Add the lv_binding_micropython.
if(MPYTHON_PRO_BOARD OR LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/lv_binding_micropython/micropython.cmake)

# add lv displayer driver module
include(${CMAKE_CURRENT_LIST_DIR}/lv_displayer/micropython.cmake)

# add esp-who module
include(${CMAKE_CURRENT_LIST_DIR}/esp-who/micropython.cmake)
if(LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/touchpad/micropython.cmake)
endif()
endif()