cmake_minimum_required(VERSION 3.21)
project(hl2_source)

file(GLOB SRC "src/*.cpp" "src/*.h" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(hl2_source PRIVATE "src/")

if (MSVC)
    # Lib path
#    target_link_directories(hl2_source PRIVATE "C:/Program Files/PothosSDR/bin/")
    target_link_libraries(hl2_source PUBLIC iphlpapi.lib )

#    target_include_directories(hl2_source PUBLIC "C:/Program Files/PothosSDR/include/libairspyhf/")

#    target_link_libraries(hl2_source PRIVATE airspyhf)
else (MSVC)
    find_package(PkgConfig)

#    pkg_check_modules(LIBAIRSPYHF REQUIRED libairspyhf)

#    target_include_directories(hl2_source PRIVATE )
#    target_link_directories(hl2_source PRIVATE )

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(hl2_source PRIVATE "/usr/local/include")
    endif()
    
endif ()
