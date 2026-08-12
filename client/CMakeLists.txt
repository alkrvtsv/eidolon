find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(ffmpeg REQUIRED)

add_executable(ClientWorker 
    src/main.cpp 
    src/FFmpegDecoder.cpp 
    src/WebRTCClient.cpp
)

target_link_libraries(ClientWorker PRIVATE 
    LibDataChannel::LibDataChannel
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    nlohmann_json::nlohmann_json
    ${FFMPEG_LIBRARIES}
)

target_include_directories(ClientWorker PRIVATE ${FFMPEG_INCLUDE_DIRS})