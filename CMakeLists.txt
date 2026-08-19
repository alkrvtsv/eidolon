cmake_minimum_required(VERSION 3.15)
project(LowLatencyStreamer VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(host)
add_subdirectory(client)