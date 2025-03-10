# Add the lv_binding_micropython.
if(MPYTHON_PRO_BOARD OR LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/lv_binding_micropython/bindings.cmake)

# add lcd driver module
include(${CMAKE_CURRENT_LIST_DIR}/lcd/micropython.cmake)
endif()

# add esp-who module
if(LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/esp-who/micropython.cmake)
endif()
# include(${CMAKE_CURRENT_LIST_DIR}/cppexample/micropython.cmake)