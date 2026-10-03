set(SDRPP_MODULE_DEFAULTS "DEFAULT" CACHE STRING "Default module set: DEFAULT or MINIMAL")
set_property(CACHE SDRPP_MODULE_DEFAULTS PROPERTY STRINGS DEFAULT MINIMAL)
if (SDRPP_MODULE_DEFAULTS STREQUAL "MINIMAL")
    set(SDRPP_DEFAULT_MODULE OFF)
elseif (SDRPP_MODULE_DEFAULTS STREQUAL "DEFAULT")
    set(SDRPP_DEFAULT_MODULE ON)
else()
    message(FATAL_ERROR "SDRPP_MODULE_DEFAULTS must be DEFAULT or MINIMAL")
endif()


# GLFW is the desktop backend. Reject old Android configurations explicitly.
if (ANDROID OR OPT_BACKEND_ANDROID)
    message(FATAL_ERROR "Android builds are no longer supported; use a desktop toolchain")
endif()
unset(OPT_BACKEND_ANDROID CACHE)
unset(OPT_BACKEND_GLFW CACHE)

# Compatibility Options
option(OPT_OVERRIDE_STD_FILESYSTEM "Use a local version of std::filesystem on systems that don't have it yet" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/SDRPPModules.cmake")

# Other options
option(USE_BUNDLE_DEFAULTS "Set the default resource and module directories to the right ones for a MacOS .app" OFF)
option(COPY_MSVC_REDISTRIBUTABLES "Copy over the Visual C++ Redistributable" OFF)
option(BUILD_TESTS "Build test suite" OFF)

option(SDRPP_USE_BUNDLED_VOLK "Build the pinned VOLK dependency from source instead of using an installed package" OFF)

if (WIN32)
    set(SDRPLAY_ROOT "C:/Program Files/SDRplay/API" CACHE PATH "SDRplay API installation directory")

    set(SDRPP_RUNTIME_INSTALL_DIR ".")
    set(SDRPP_MODULE_INSTALL_DIR "modules")
    set(SDRPP_RESOURCE_INSTALL_DIR "res")
else()
    set(SDRPP_RUNTIME_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
    set(SDRPP_MODULE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/sdrpp/plugins")
    set(SDRPP_RESOURCE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/sdrpp")
endif()
