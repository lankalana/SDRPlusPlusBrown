cmake_minimum_required(VERSION 3.21)
project(scanner)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(scanner PRIVATE "src/")