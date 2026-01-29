include(${CMAKE_CURRENT_LIST_DIR}/esp_lvgl_port/lvgl_port.cmake)

add_library(usermod_display INTERFACE)

target_sources(usermod_display INTERFACE
        ${PORT_PATH}/esp_lvgl_port.c
        ${PORT_PATH}/esp_lvgl_port_disp.c
        ${ADD_SRCS}
        ${CMAKE_CURRENT_LIST_DIR}/modlvdisplayer.c
)

target_include_directories(usermod_display INTERFACE
        # ${IDF_PATH}/components/esp_lcd/include/
        # ${CMAKE_CURRENT_LIST_DIR}/../esp-who/esp-who/components/modules/lcd
        ${CMAKE_CURRENT_LIST_DIR}/../lv_binding_micropython/lvgl
        ${CMAKE_CURRENT_LIST_DIR}/../lv_binding_micropython/lvgl/src
        ${CMAKE_CURRENT_LIST_DIR}/esp_lvgl_port/include
        ${CMAKE_CURRENT_LIST_DIR}/esp_lvgl_port/priv_include
        ${CMAKE_CURRENT_LIST_DIR}/../../../esp-gmf/packages/esp_board_manager/devices
        ${CMAKE_CURRENT_LIST_DIR}/../../../esp-gmf/packages/esp_board_manager/include
        # ${ESP_COMMON_INCLUDE_DIRS}
)

target_compile_definitions(usermod_display INTERFACE
    ESP_PLATFORM=1  
)

target_link_libraries(usermod INTERFACE usermod_display)
