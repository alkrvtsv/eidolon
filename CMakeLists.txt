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

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_target(generate_structure_dump ALL
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/structure.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Post-build: Generating updated project structure dump..."
    VERBATIM
)

add_dependencies(generate_structure_dump eidolon_host eidolon_client)