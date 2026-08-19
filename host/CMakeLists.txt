project(HostWorker)

find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(HostWorker 
    src/main.cpp 
    src/DXGICapture.cpp
    src/SignalingClient.cpp 
    src/WebRTCManager.cpp 
    src/NVENCEncoder.cpp
)

target_link_libraries(HostWorker PRIVATE
    LibDataChannel::LibDataChannel 
    nlohmann_json::nlohmann_json 
    d3d11 
    dxgi
)

add_executable(ListMonitors src/ListMonitors.cpp)

target_link_libraries(ListMonitors PRIVATE 
    d3d11 
    dxgi
)