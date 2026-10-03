cmake_minimum_required(VERSION 3.21)
project(vor_receiver)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(vor_receiver PRIVATE "src/")
