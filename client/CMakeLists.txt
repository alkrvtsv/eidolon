find_package(LibDataChannel CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(ffmpeg REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(eidolon_client
    src/main.cpp
)

target_include_directories(eidolon_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(eidolon_client PRIVATE
    eidolon_common
    LibDataChannel::LibDataChannel
    Opus::opus
    nlohmann_json::nlohmann_json
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    ${FFMPEG_LIBRARIES}
    d3d11.lib
    dxgi.lib
    dxguid.lib
    imm32.lib
    version.lib
    winmm.lib
)