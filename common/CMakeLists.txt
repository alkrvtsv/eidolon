add_library(eidolon_common INTERFACE)

target_include_directories(eidolon_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eidolon_common INTERFACE
    avrt.lib
)