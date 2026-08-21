cmake_minimum_required(VERSION 3.13)
project(sdrplay_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    add_library(sdrplay_api_imported SHARED IMPORTED)
    set_target_properties(sdrplay_api_imported PROPERTIES
        IMPORTED_IMPLIB "C:/Program Files/SDRplay/API/x64/sdrplay_api.lib"
        IMPORTED_LOCATION "C:/Program Files/SDRplay/API/x64/sdrplay_api.dll"
    )
    target_include_directories(sdrplay_source PRIVATE "C:/Program Files/SDRplay/API/inc")

    target_link_libraries(sdrplay_source PRIVATE sdrplay_api_imported)
else (MSVC)
    find_package(PkgConfig)

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(sdrplay_source PRIVATE "/usr/local/include")
        target_link_directories(sdrplay_source PRIVATE "/usr/local/lib/")
    endif()

    target_link_libraries(sdrplay_source PRIVATE sdrplay_api)
endif ()
