cmake_minimum_required(VERSION 3.21)
project(audio_sink)

file(GLOB SRC CONFIGURE_DEPENDS "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    find_package(RtAudio CONFIG REQUIRED)
    target_link_libraries(audio_sink PRIVATE RtAudio::rtaudio)
else (MSVC)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(RTAUDIO REQUIRED IMPORTED_TARGET rtaudio)


    target_link_libraries(audio_sink PRIVATE PkgConfig::RTAUDIO)

endif ()
