# Board information output
message(STATUS "Selected Board: mpython_v3")
message(STATUS "Board Path: ../esp-gmf/packages/esp_board_manager/boards/mpython_v3")

idf_component_register(
    SRC_DIRS "." "../../../esp-gmf/packages/esp_board_manager/boards/mpython_v3"
    INCLUDE_DIRS "." "../../../esp-gmf/packages/esp_board_manager/boards/mpython_v3"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
