# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp-code-scanner)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp-dl)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/fb_gfx)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera)

add_library(usermod_expample INTERFACE)

target_sources(usermod_expample INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/mod/test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mod/modtest.c
)

set(ESP_CODE_SCANNER_INCLUDEDIRS   ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp-code-scanner/include)
set(ESP32_CAMERA_INCLUDEDIRS 
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera/driver/include
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera/conversions/include
)
set(MODULES_INCLUDEDIRS 
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/ai
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/camera
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/lcd
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/web
)

target_include_directories(usermod_expample INTERFACE
        ${ESP_CODE_SCANNER_INCLUDEDIRS}
        ${ESP32_CAMERA_INCLUDEDIRS}
        ${MODULES_INCLUDEDIRS}
)

target_link_libraries(usermod INTERFACE usermod_expample)

