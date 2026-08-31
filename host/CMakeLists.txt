find_package(LibDataChannel CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(eidolon_host
    src/main.cpp
    src/capture/dxgi_capturer.cpp
    src/color_converter/d3d11_video_processor.cpp
    src/encoder/nvenc_encoder.cpp
    src/audio/wasapi_opus_capturer.cpp
    src/input/windows_input_injector.cpp
    src/network/signaling_client.cpp
    src/network/webrtc_streamer.cpp
)

target_include_directories(eidolon_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nvenc/include
)

target_link_libraries(eidolon_host PRIVATE
    eidolon_common
    LibDataChannel::LibDataChannel
    Opus::opus
    nlohmann_json::nlohmann_json
    d3d11.lib
    dxgi.lib
    dxguid.lib
    mfplat.lib
    mfuuid.lib
    mmdevapi.lib
    avrt.lib
    user32.lib
)