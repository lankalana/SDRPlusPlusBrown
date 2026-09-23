cmake_minimum_required(VERSION 3.21)
project(aprs_decoder)

file(GLOB_RECURSE SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(aprs_decoder PRIVATE "src/")

if(BUILD_TESTS)
    add_executable(aprs_decoder_tests
        tests/aprs_decoder_tests.cpp
        src/aprs_decoder.cpp
    )
    target_include_directories(aprs_decoder_tests PRIVATE "src/")
    add_test(NAME aprs_decoder_tests COMMAND aprs_decoder_tests)
endif()
