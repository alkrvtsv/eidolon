cmake_minimum_required(VERSION 3.15)
project(LowLatencyStreamer VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Добавляем поддиректории, когда они будут готовы
# add_subdirectory(common)
add_subdirectory(host)
# add_subdirectory(client)
