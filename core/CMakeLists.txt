cmake_minimum_required(VERSION 3.21)
project(sdrpp_core)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
    "src/*.cpp" "src/*.c"
    "../misc_modules/noise_reduction_logmmse/src/omlsa_mcra/*.cpp"
    "../misc_modules/noise_reduction_logmmse/src/omlsa_mcra.cpp"
)
list(FILTER SRC EXCLUDE REGEX "sdrpp_ft8_mshv_main.cpp")

file(GLOB_RECURSE BACKEND_SRC CONFIGURE_DEPENDS "backends/glfw/*.cpp" "backends/glfw/*.c")

add_library(sdrpp_core SHARED ${SRC} ${BACKEND_SRC})
sdrpp_set_compile_options(sdrpp_core)
target_compile_definitions(sdrpp_core PRIVATE SDRPP_IS_CORE)
target_compile_definitions(sdrpp_core PUBLIC INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}")
target_include_directories(sdrpp_core PUBLIC src src/imgui)

if (USE_BUNDLE_DEFAULTS)
    target_compile_definitions(sdrpp_core PRIVATE IS_MACOS_BUNDLE)
endif()
if (BUILD_TESTS)
    target_compile_definitions(sdrpp_core PUBLIC BUILD_TESTS)
endif()
if (MSVC)
    set_target_properties(sdrpp_core PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
endif()

if (APPLE)
    target_link_libraries(sdrpp_core PUBLIC
        "-framework CoreFoundation" "-framework Accelerate" "-framework Security"
        "-framework CFNetwork" "-framework AudioToolbox"
    )
endif()

target_include_directories(sdrpp_core PUBLIC backends/glfw backends/glfw/imgui)
if (MSVC)
    find_package(glfw3 CONFIG REQUIRED)
    target_link_libraries(sdrpp_core PUBLIC glfw)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
    target_link_libraries(sdrpp_core PUBLIC PkgConfig::GLFW3)
endif()

if (OPT_OVERRIDE_STD_FILESYSTEM)
    target_include_directories(sdrpp_core PUBLIC std_replacement)
endif()

if (MSVC)
    find_package(OpenGL REQUIRED)
    find_package(FFTW3f CONFIG REQUIRED)
    find_package(zstd CONFIG REQUIRED)
    target_compile_definitions(sdrpp_core PRIVATE SDRPP_FFTW_THREADS)
    target_link_libraries(sdrpp_core PUBLIC
        ${SDRPP_VOLK_TARGET} OpenGL::GL FFTW3::fftw3f
        wsock32 ws2_32 iphlpapi zstd::libzstd_shared
    )
else()
    find_package(PkgConfig REQUIRED)
    find_package(OpenGL REQUIRED)
    pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
    pkg_check_modules(FFTW3F_THREADS QUIET IMPORTED_TARGET fftw3f_threads)
    pkg_check_modules(LIBZSTD REQUIRED IMPORTED_TARGET libzstd)
    target_link_libraries(sdrpp_core PUBLIC
        OpenGL::GL PkgConfig::FFTW3F PkgConfig::LIBZSTD
    )
    if (FFTW3F_THREADS_FOUND)
        target_compile_definitions(sdrpp_core PRIVATE SDRPP_FFTW_THREADS)
        target_link_libraries(sdrpp_core PUBLIC PkgConfig::FFTW3F_THREADS)
    endif()
    pkg_check_modules(VOLK REQUIRED IMPORTED_TARGET volk)
    target_link_libraries(sdrpp_core PUBLIC PkgConfig::VOLK)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OPT_OVERRIDE_STD_FILESYSTEM)
        target_link_libraries(sdrpp_core PUBLIC stdc++fs)
    endif()
endif()

if (WIN32)
    install(TARGETS sdrpp_core
        RUNTIME_DEPENDENCY_SET sdrpp_runtime_dependencies
        RUNTIME DESTINATION "${SDRPP_RUNTIME_INSTALL_DIR}"
    )
else()
    install(TARGETS sdrpp_core
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
