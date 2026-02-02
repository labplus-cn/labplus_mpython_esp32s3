add_library(audio_module INTERFACE)

target_sources(audio_module INTERFACE
    # ${CMAKE_CURRENT_LIST_DIR}/audio_player.c
    # ${CMAKE_CURRENT_LIST_DIR}/audio_recorder.c
    # ${CMAKE_CURRENT_LIST_DIR}/src/lfs2_stream.c
    # ${CMAKE_CURRENT_LIST_DIR}/src/vfs_lfs2.c
    ${CMAKE_CURRENT_LIST_DIR}/modaudio.c
)

target_include_directories(audio_module INTERFACE
    # ${MPY_PORT_DIR}/components/audio_stream/include
    # ${MPY_PORT_DIR}/components/audio_stream/lib/gzip/include
    # ${MPY_PORT_DIR}/components/audio_stream/lib/hls/include
    # ${MPY_PORT_DIR}/components/audio_pipeline/include
    # ${MPY_PORT_DIR}/components/audio_sal/include
    # ${MPY_PORT_DIR}/components/audio_hal/include
    # ${MPY_PORT_DIR}/components/audio_board/include
    # ${MPY_PORT_DIR}/components/audio_board/mpython
    # ${MPY_PORT_DIR}/components/esp-adf-libs/esp_audio/include
    # ${MPY_PORT_DIR}/components/esp-adf-libs/esp_codec/include/codec
    # ${MPY_PORT_DIR}/components/esp-adf-libs/esp_codec/include/processing
    # ${CMAKE_CURRENT_LIST_DIR}/include
    ${MPY_PORT_DIR}/../esp-gmf/packages/esp_audio_simple_player/include
    ${MPY_PORT_DIR}/../esp-gmf/packages/gmf_app_utils/include
    ${MPY_PORT_DIR}/../esp-gmf/packages/gmf_loader/include
    ${MPY_PORT_DIR}/../esp-gmf/gmf_core/include
    ${MPY_PORT_DIR}/../esp-gmf/elements/gmf_audio/include
    ${MPY_PORT_DIR}/../esp-gmf/elements/
)

target_link_libraries(usermod INTERFACE audio_module)