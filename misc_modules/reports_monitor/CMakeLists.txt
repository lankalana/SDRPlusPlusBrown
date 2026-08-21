cmake_minimum_required(VERSION 3.21)
project(reports_monitor)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(reports_monitor PRIVATE "src/")