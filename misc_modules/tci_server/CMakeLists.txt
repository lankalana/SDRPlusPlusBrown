cmake_minimum_required(VERSION 3.21)
project(tci_server)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(tci_server PRIVATE "src/")
target_include_directories(tci_server PRIVATE "../recorder/src")
target_include_directories(tci_server PRIVATE "../../decoder_modules/meteor_demodulator/src")
target_include_directories(tci_server PRIVATE "../../decoder_modules/radio/src")
