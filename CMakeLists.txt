cmake_minimum_required(VERSION 3.20)
project(EidolonStream LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /WX /permissive- /Zc:preprocessor /utf-8)
endif()

add_subdirectory(common)
add_subdirectory(host)
add_subdirectory(client)