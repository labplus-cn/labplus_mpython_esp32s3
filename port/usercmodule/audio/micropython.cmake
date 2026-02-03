add_library(audio_module INTERFACE)

target_sources(audio_module INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/tts/tts.c
    ${CMAKE_CURRENT_LIST_DIR}/src/audio_tts.c
    ${CMAKE_CURRENT_LIST_DIR}/modaudio.c
)

target_include_directories(audio_module INTERFACE
    ${MPY_PORT_DIR}/../esp-gmf/packages/esp_audio_simple_player/include
    ${MPY_PORT_DIR}/../esp-gmf/packages/gmf_app_utils/include
    ${MPY_PORT_DIR}/../esp-gmf/packages/gmf_loader/include
    ${MPY_PORT_DIR}/../esp-gmf/gmf_core/include
    ${MPY_PORT_DIR}/../esp-gmf/elements/gmf_audio/include
    ${MPY_PORT_DIR}/../esp-gmf/elements/
    ${CMAKE_CURRENT_LIST_DIR}/src/tts
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${MPY_PORT_DIR}/../esp-gmf/packages/gmf_app_utils/include
)

target_link_libraries(usermod INTERFACE audio_module)
