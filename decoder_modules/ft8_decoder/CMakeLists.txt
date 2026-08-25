cmake_minimum_required(VERSION 3.21)
project(ft8_decoder)


file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

if (ANDROID)
    target_link_libraries(ft8_decoder PRIVATE log)
endif()

target_include_directories(ft8_decoder PRIVATE "src/")
