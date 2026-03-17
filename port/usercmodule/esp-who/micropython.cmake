set(MODULES_INCLUDEDIRS 
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/ai
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/camera
        # ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/lcd
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/web
)

set(ESP32_CAMERA_INCLUDEDIRS 
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera/driver/include
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera/conversions/include
)


add_library(usermod_sensor INTERFACE)

target_sources(usermod_sensor INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/mod/modsensor.c
)

target_include_directories(usermod_sensor INTERFACE
        ${MODULES_INCLUDEDIRS}
        ${ESP32_CAMERA_INCLUDEDIRS}
)

target_link_libraries(usermod INTERFACE usermod_sensor)

add_library(usermod_AIcamera INTERFACE)

target_sources(usermod_AIcamera INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/mod/modAIcamera.c
)

target_include_directories(usermod_AIcamera INTERFACE
        ${MODULES_INCLUDEDIRS}
        ${ESP32_CAMERA_INCLUDEDIRS}
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/lcd
        ${CMAKE_CURRENT_LIST_DIR}/../../../esp-gmf/packages/esp_board_manager/include
        ${CMAKE_CURRENT_LIST_DIR}/../../../esp-gmf/packages/esp_board_manager/devices/dev_display_lcd
)

target_link_libraries(usermod INTERFACE usermod_AIcamera)

add_compile_options(-fdiagnostics-color=always)

