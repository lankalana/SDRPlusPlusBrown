cmake_minimum_required(VERSION 3.21)
project(noise_reduction_logmmse)

set(SRC
    src/main.cpp
    src/omlsa_mcra.cpp
    src/omlsa_mcra/base4_fft.cpp
    src/omlsa_mcra/Cordic.cpp
    src/omlsa_mcra/Datablock_Read.cpp
    src/omlsa_mcra/G_calculate.cpp
    src/omlsa_mcra/LSA_denoise.cpp
)

include(${SDRPP_MODULE_CMAKE})

target_include_directories(noise_reduction_logmmse PRIVATE "src/")
target_include_directories(noise_reduction_logmmse PRIVATE "src/omlsa_mcra")
target_include_directories(noise_reduction_logmmse PRIVATE "../../decoder_modules/radio/src")
