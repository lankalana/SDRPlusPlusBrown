cmake_minimum_required(VERSION 3.21)
project(null_audio_sink)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})
