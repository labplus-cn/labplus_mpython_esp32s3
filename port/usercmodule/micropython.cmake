# Add the lv_binding_micropython.
if(MPYTHON_PRO_BOARD OR LABPLUS_LEDONG_V2_BOARD)
include(${CMAKE_CURRENT_LIST_DIR}/lv_binding_micropython/bindings.cmake)

# add lcd driver module
include(${CMAKE_CURRENT_LIST_DIR}/lcd/micropython.cmake)

# add esp-who module
include(${CMAKE_CURRENT_LIST_DIR}/esp-who/micropython.cmake)
# include(${CMAKE_CURRENT_LIST_DIR}/cppexample/micropython.cmake)
endif()