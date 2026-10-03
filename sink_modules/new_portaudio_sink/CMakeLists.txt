cmake_minimum_required(VERSION 3.21)
project(new_portaudio_sink)

file(GLOB SRC CONFIGURE_DEPENDS "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    find_package(portaudio CONFIG REQUIRED)
    target_link_libraries(new_portaudio_sink PRIVATE portaudio)
else (MSVC)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)



    target_link_libraries(new_portaudio_sink PRIVATE PkgConfig::PORTAUDIO)

endif (MSVC)
