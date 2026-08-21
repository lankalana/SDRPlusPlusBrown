cmake_minimum_required(VERSION 3.21)
project(network_sink)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})