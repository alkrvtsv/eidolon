project(HostWorker)

# Находим WebRTC и JSON
find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Добавляем исходники для основного хоста
add_executable(HostWorker 
    src/main.cpp 
    src/DXGICapture.cpp
    src/SignalingClient.cpp 
    src/WebRTCManager.cpp 
    src/NVENCEncoder.cpp
)

# Подключаем библиотеки для основного хоста
target_link_libraries(HostWorker PRIVATE
    LibDataChannel::LibDataChannel 
    nlohmann_json::nlohmann_json 
    d3d11 
    dxgi
)

# --- Тестовая утилита для проверки мониторов ---
# Добавляем отдельный экзешник
add_executable(ListMonitors src/ListMonitors.cpp)

# Подключаем ТОЛЬКО системные библиотеки DirectX
target_link_libraries(ListMonitors PRIVATE 
    d3d11 
    dxgi
)