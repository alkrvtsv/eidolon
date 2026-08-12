project(HostWorker)

# Находим WebRTC и JSON
find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Добавляем исходники (сейчас добавим новый файл)
add_executable(HostWorker 
    src/main.cpp
    src/DXGICapture.cpp
    src/SignalingClient.cpp
    src/WebRTCManager.cpp
    src/NVENCEncoder.cpp
)

# Подключаем библиотеки
target_link_libraries(HostWorker PRIVATE 
    LibDataChannel::LibDataChannel
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
)