set(COMPONENT_ADD_INCLUDEDIRS 
    ./include
    ./i2c_bus)

# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES audio_sal audio_hal driver)

message(STATUS "Current board name is " CONFIG_ESP_LYRAT_V4_3_BOARD)
list(APPEND COMPONENT_ADD_INCLUDEDIRS ./mpython)
set(COMPONENT_SRCS
./mpython/board.c
./mpython/board_pins_config.c
./i2c_bus/i2c_bus.c
# ./i2c_bus/i2c_bus_v2.c
)

register_component()
