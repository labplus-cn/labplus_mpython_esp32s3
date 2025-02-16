add_library(usermod_lcd INTERFACE)

target_sources(usermod_lcd INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/spi.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display.c
        ${CMAKE_CURRENT_LIST_DIR}/src/wrapper.c
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
)

target_include_directories(usermod_lcd INTERFACE
        ${IDF_PATH}/components/esp_lcd/include/
        ${CMAKE_CURRENT_LIST_DIR}/../lv_binding_micropython/lvgl
        ${CMAKE_CURRENT_LIST_DIR}/../lv_binding_micropython/lvgl/src
)

target_link_libraries(usermod INTERFACE usermod_lcd)
