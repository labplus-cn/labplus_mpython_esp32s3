# Add the lv_binding_micropython.
include(${CMAKE_CURRENT_LIST_DIR}/lv_binding_micropython/bindings.cmake)

# add lcd driver module
include(${CMAKE_CURRENT_LIST_DIR}/lcd/micropython.cmake)