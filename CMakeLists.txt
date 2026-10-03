cmake_minimum_required(VERSION 3.21)
if (POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
project(sdrpp LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SDRPPOptions)
include(SDRPPCompiler)

set(SDRPP_MODULE_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/sdrpp_module.cmake")
set(SDRPP_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/core")

# Preserve the platform-specific package layout.
if (UNIX AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if (APPLE)
        set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Install prefix" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
    endif()
endif()

configure_file(cmake_uninstall.cmake cmake_uninstall.cmake @ONLY)
add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    VERBATIM
)

include(SDRPPVolk)
add_subdirectory(core)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

sdrpp_add_modules()

set(SDRPP_SOURCES src/main.cpp)
if (WIN32)
    list(APPEND SDRPP_SOURCES win32/resources.rc)
endif()
if (APPLE)
    list(APPEND SDRPP_SOURCES src/macos_init.m)
    set_source_files_properties(src/macos_init.m PROPERTIES COMPILE_FLAGS "-std=c99")
endif()

add_executable(sdrpp ${SDRPP_SOURCES})
target_link_libraries(sdrpp PRIVATE sdrpp_core)
sdrpp_set_compile_options(sdrpp)

if (APPLE)
    target_link_libraries(sdrpp PRIVATE "-framework Foundation" "-framework AVFoundation" "-framework Accelerate")
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_link_libraries(sdrpp PRIVATE pthread)
endif()

include(SDRPPRuntime)
configure_file(sdrpp_module.cmake sdrpp_module.cmake @ONLY)
include(SDRPPInstall)
