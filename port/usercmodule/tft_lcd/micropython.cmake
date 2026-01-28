add_library(usermod_lcd INTERFACE)

target_sources(usermod_lcd INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/modlcd.c
)

set(ESP_COMPONENTS_LCD_INCLUDEDIRS
        ${IDF_PATH}/components/esp_lcd/include/
        ${IDF_PATH}/components/esp_lcd/interface/
)

set(ESP_BOARD_MANAGER_INCLUDEDIRS 
        ${CMAKE_CURRENT_LIST_DIR}/../../../esp-gmf/packages/esp_board_manager/include
        ${CMAKE_CURRENT_LIST_DIR}/../../../esp-gmf/packages/esp_board_manager/devices/dev_display_lcd
)

target_include_directories(usermod_lcd INTERFACE
        ${ESP_BOARD_MANAGER_INCLUDEDIRS}
        ${ESP_COMPONENTS_LCD_INCLUDEDIRS}
)

target_link_libraries(usermod INTERFACE usermod_lcd)

add_compile_options(-fdiagnostics-color=always)

