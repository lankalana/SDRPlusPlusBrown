cmake_minimum_required(VERSION 3.21)
project(hackrf_source)

file(GLOB SRC CONFIGURE_DEPENDS "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    find_package(hackrf CONFIG REQUIRED)

    target_link_libraries(hackrf_source PRIVATE hackrf::hackrf)

    # hackrf.dll itself imports libusb-1.0.dll and pthreadVC3.dll. Those are not
    # exposed as CMake targets, so $<TARGET_RUNTIME_DLLS> does not deploy them
    # and loading the module fails with error 126.
    add_custom_command(
        TARGET hackrf_source
        POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:hackrf::hackrf>/libusb-1.0.dll"
            "$<TARGET_FILE_DIR:hackrf::hackrf>/pthreadVC3.dll"
            "${CMAKE_BINARY_DIR}/$<CONFIG>"

        COMMENT "Deploying hackrf runtime dependencies"
        VERBATIM
    )
else (MSVC)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(LIBHACKRF REQUIRED IMPORTED_TARGET libhackrf)


    target_link_libraries(hackrf_source PRIVATE PkgConfig::LIBHACKRF)
endif ()
