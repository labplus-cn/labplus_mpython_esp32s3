##
## micropython.cmake - 小智AI语音会话模块构建配置
##
## 依赖组件:
##   - esp_audio_codec (Opus 编解码)
##   - esp_audio_effects (采样率转换，可选)
##   - esp_codec_dev (音频硬件 I/O)
##   - esp_gmf_app_utils (GMF 外设工具)
##   - esp_websocket_client (WebSocket 通信)
##   - cJSON (JSON 解析，ESP-IDF 内置)
##
## 功能开关（生产版本关闭以节省约 4KB flash + 1KB RAM）：
##   cmake -DXIAOZHI_ENABLE_REC_TO_FILE=OFF ...
##

## record_to_file 录音测试功能（默认开启；关闭后 xiaozhi.record_to_file 不可用）
# option(XIAOZHI_ENABLE_REC_TO_FILE "Include record-to-file test feature" OFF)

add_library(xiaozhi_module INTERFACE)

target_sources(xiaozhi_module INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/xiaozhi_session.c
    ${CMAKE_CURRENT_LIST_DIR}/src/audio_capture.c
    ${CMAKE_CURRENT_LIST_DIR}/src/activate.c
    ${CMAKE_CURRENT_LIST_DIR}/modxiaozhi.c
)

# if(XIAOZHI_ENABLE_REC_TO_FILE)
#     target_sources(xiaozhi_module INTERFACE
#         ${CMAKE_CURRENT_LIST_DIR}/src/rec_to_file.c
#         # vfs_lfs2: mpython_v3 板级 cmake 未编译此文件，在此补充
#         ${MPY_PORT_DIR}/drivers/audio/vfs_lfs2.c
#     )
#     target_compile_definitions(xiaozhi_module INTERFACE XIAOZHI_ENABLE_REC_TO_FILE=1)
# endif()

target_include_directories(xiaozhi_module INTERFACE
    # 本模块头文件
    ${CMAKE_CURRENT_LIST_DIR}/src
    # ${CMAKE_CURRENT_LIST_DIR}/include

    # GMF 应用工具 (esp_gmf_app_setup_peripheral.h)
    ${MPY_PORT_DIR}/../esp-gmf/packages/gmf_app_utils/include

    # GMF Core (esp_gmf_pool.h)
    ${MPY_PORT_DIR}/../esp-gmf/gmf_core/include

    # GMF Audio Elements (esp_gmf_ch_cvt.h / bit_cvt / rate_cvt)
    ${MPY_PORT_DIR}/../esp-gmf/elements/gmf_audio/include

    # GMF Audio Render (esp_audio_render.h)
    ${MPY_PORT_DIR}/../esp-gmf/packages/esp_audio_render/include

    # GMF Capture (esp_capture.h / esp_capture_sink.h)
    ${MPY_PORT_DIR}/../esp-gmf/packages/esp_capture/include
    ${MPY_PORT_DIR}/../esp-gmf/packages/esp_capture/include/impl

    # GMF AI Audio (esp_gmf_afe.h / esp_gmf_afe_manager.h)
    ${MPY_PORT_DIR}/../esp-gmf/elements/gmf_ai_audio/include

    # ESP-SR 模型加载 (model_path.h)
    ${MPY_PORT_DIR}/managed_components/espressif__esp-sr/src/include

    # ESP-SR AFE/WakeNet API (esp_afe_sr_models.h / esp_afe_config.h)
    ${MPY_PORT_DIR}/managed_components/espressif__esp-sr/include/esp32s3

    # ESP Audio Codec (Opus 编解码器)
    ${MPY_PORT_DIR}/managed_components/espressif__esp_audio_codec/include
    ${MPY_PORT_DIR}/managed_components/espressif__esp_audio_codec/include/encoder
    ${MPY_PORT_DIR}/managed_components/espressif__esp_audio_codec/include/encoder/impl
    ${MPY_PORT_DIR}/managed_components/espressif__esp_audio_codec/include/decoder
    ${MPY_PORT_DIR}/managed_components/espressif__esp_audio_codec/include/decoder/impl

    # ESP Codec Device (音频硬件 I/O)
    ${MPY_PORT_DIR}/managed_components/espressif__esp_codec_dev/include
)

target_link_libraries(usermod INTERFACE xiaozhi_module)
