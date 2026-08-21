cmake_minimum_required(VERSION 3.21)
project(adsb_decoder)

file(GLOB_RECURSE SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(adsb_decoder PRIVATE "src/")

if(BUILD_TESTS)
    add_executable(adsb_decoder_tests
        tests/adsb_decoder_tests.cpp
        src/adsb_decoder.cpp
    )
    target_include_directories(adsb_decoder_tests PRIVATE "src/")
    add_test(NAME adsb_decoder_tests COMMAND adsb_decoder_tests)
endif()
