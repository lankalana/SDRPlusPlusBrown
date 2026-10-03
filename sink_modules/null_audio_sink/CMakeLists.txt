cmake_minimum_required(VERSION 3.21)
project(null_audio_sink)

file(GLOB SRC CONFIGURE_DEPENDS "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})
