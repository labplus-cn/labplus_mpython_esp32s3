# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp-code-scanner)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp-dl)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/fb_gfx)
# list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera)
# add_library(usermod_display INTERFACE)

# target_sources(usermod_display INTERFACE
#         ${CMAKE_CURRENT_LIST_DIR}/mod/moddisplay.c
# )

set(MODULES_INCLUDEDIRS 
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/ai
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/camera
        # ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/lcd
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/modules/web
)

set(ESP32_CAMERA_INCLUDEDIRS 
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera/driver/include
        ${CMAKE_CURRENT_LIST_DIR}/esp-who/components/esp32-camera/conversions/include
        ${CMAKE_CURRENT_LIST_DIR}/../lcd
)

# target_include_directories(usermod_display INTERFACE
#         ${MODULES_INCLUDEDIRS}
#         ${ESP32_CAMERA_INCLUDEDIRS}
# )

# target_link_libraries(usermod INTERFACE usermod_display)

if(LABPLUS_LEDONG_V2_BOARD)
        add_library(usermod_sensor INTERFACE)

        target_sources(usermod_sensor INTERFACE
                ${CMAKE_CURRENT_LIST_DIR}/mod/modsensor.c
        )

        target_include_directories(usermod_sensor INTERFACE
                ${MODULES_INCLUDEDIRS}
                ${ESP32_CAMERA_INCLUDEDIRS}
        )

        target_link_libraries(usermod INTERFACE usermod_sensor)
endif()

