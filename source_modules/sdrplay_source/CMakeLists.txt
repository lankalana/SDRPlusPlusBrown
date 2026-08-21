cmake_minimum_required(VERSION 3.21)
project(sdrplay_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    find_path(SDRPLAY_INCLUDE_DIR NAMES sdrplay_api.h HINTS "${SDRPLAY_ROOT}/inc" REQUIRED)
    find_library(SDRPLAY_LIBRARY NAMES sdrplay_api HINTS "${SDRPLAY_ROOT}/x64" REQUIRED)
    find_file(SDRPLAY_RUNTIME NAMES sdrplay_api.dll HINTS "${SDRPLAY_ROOT}/x64" REQUIRED)

    add_library(SDRplay::API SHARED IMPORTED)
    set_target_properties(SDRplay::API PROPERTIES
        IMPORTED_IMPLIB "${SDRPLAY_LIBRARY}"
        IMPORTED_LOCATION "${SDRPLAY_RUNTIME}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDRPLAY_INCLUDE_DIR}"
    )
    target_link_libraries(sdrplay_source PRIVATE SDRplay::API)
else (MSVC)
    find_package(PkgConfig)

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(sdrplay_source PRIVATE "/usr/local/include")
        target_link_directories(sdrplay_source PRIVATE "/usr/local/lib/")
    endif()

    target_link_libraries(sdrplay_source PRIVATE sdrplay_api)
endif ()
