add_library(usermod_rfid INTERFACE)

target_sources(usermod_rfid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/mfrc522.c
        ${CMAKE_CURRENT_LIST_DIR}/modrfid.c
)

target_include_directories(usermod_rfid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${MPY_PORT_DIR}/../esp-gmf/packages/esp_board_manager/include
)

target_link_libraries(usermod INTERFACE usermod_rfid)
