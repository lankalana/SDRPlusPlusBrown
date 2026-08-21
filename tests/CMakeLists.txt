add_executable(dsp_bounded_tests
    dsp_bounded_tests.cpp
    ../core/src/utils/flog.cpp
)
target_include_directories(dsp_bounded_tests PRIVATE ../core/src)
target_link_libraries(dsp_bounded_tests PRIVATE ${SDRPP_VOLK_TARGET})
target_compile_options(dsp_bounded_tests PRIVATE ${SDRPP_COMPILER_FLAGS})
if (MSVC)
    add_custom_command(TARGET dsp_bounded_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:dsp_bounded_tests>
            $<TARGET_FILE_DIR:dsp_bounded_tests>
        COMMAND_EXPAND_LISTS
    )
endif()
add_test(NAME dsp_bounded_tests COMMAND dsp_bounded_tests)
add_executable(lora_phy_tests
    lora_phy_tests.cpp
    ../core/src/dsp/protocol/lora/chirp.cpp
    ../core/src/dsp/protocol/lora/decoder.cpp
    ../core/src/dsp/protocol/lora/detail/fft_symbol.cpp
    ../core/src/utils/flog.cpp
)
target_include_directories(lora_phy_tests PRIVATE ../core/src)
if (MSVC)
    find_package(FFTW3f CONFIG REQUIRED)
    target_link_libraries(lora_phy_tests PRIVATE FFTW3::fftw3f ${SDRPP_VOLK_TARGET})
    add_custom_command(TARGET lora_phy_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:FFTW3::fftw3f>
            $<TARGET_FILE_DIR:lora_phy_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${SDRPP_VOLK_TARGET}>
            $<TARGET_FILE_DIR:lora_phy_tests>
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LORA_FFTW3F REQUIRED fftw3f)
    target_include_directories(lora_phy_tests PRIVATE ${LORA_FFTW3F_INCLUDE_DIRS})
    target_link_directories(lora_phy_tests PRIVATE ${LORA_FFTW3F_LIBRARY_DIRS})
    target_link_libraries(lora_phy_tests PRIVATE ${LORA_FFTW3F_LIBRARIES} ${SDRPP_VOLK_TARGET})
endif()
target_compile_options(lora_phy_tests PRIVATE ${SDRPP_COMPILER_FLAGS})
add_test(NAME lora_phy_tests COMMAND lora_phy_tests)

set(MESHTASTIC_CORE_SOURCES
    ../core/src/protocol/meshtastic/channel.cpp
    ../core/src/protocol/meshtastic/crypto.cpp
    ../core/src/protocol/meshtastic/decoder.cpp
    ../core/src/protocol/meshtastic/packet.cpp
    ../core/src/protocol/meshtastic/protobuf.cpp
    ../core/src/protocol/meshtastic/radio_profile.cpp
)

add_executable(meshtastic_efl_tests
    meshtastic_efl_tests.cpp
    ${MESHTASTIC_CORE_SOURCES}
)
target_include_directories(meshtastic_efl_tests PRIVATE ../core/src)
if (WIN32)
    target_link_libraries(meshtastic_efl_tests PRIVATE bcrypt)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(meshtastic_efl_tests PRIVATE OpenSSL::Crypto)
endif()
target_compile_options(meshtastic_efl_tests PRIVATE ${SDRPP_COMPILER_FLAGS})
add_test(NAME meshtastic_efl_tests COMMAND meshtastic_efl_tests)

set(MESHCORE_CORE_SOURCES
    ../core/src/protocol/meshcore/channel.cpp
    ../core/src/protocol/meshcore/crypto.cpp
    ../core/src/protocol/meshcore/decoder.cpp
    ../core/src/protocol/meshcore/packet.cpp
    ../core/src/protocol/meshcore/radio_profile.cpp
)

add_executable(meshcore_tests
    meshcore_tests.cpp
    ${MESHCORE_CORE_SOURCES}
)
target_include_directories(meshcore_tests PRIVATE ../core/src)
if (WIN32)
    target_link_libraries(meshcore_tests PRIVATE bcrypt)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(meshcore_tests PRIVATE OpenSSL::Crypto)
endif()
target_compile_options(meshcore_tests PRIVATE ${SDRPP_COMPILER_FLAGS})
add_test(NAME meshcore_tests COMMAND meshcore_tests)

add_executable(lora_capture_probe
    lora_capture_probe.cpp
    ${MESHTASTIC_CORE_SOURCES}
    ../core/src/dsp/protocol/lora/chirp.cpp
    ../core/src/dsp/protocol/lora/decoder.cpp
    ../core/src/dsp/protocol/lora/detail/fft_symbol.cpp
    ../core/src/utils/flog.cpp
)
target_include_directories(lora_capture_probe PRIVATE ../core/src)
if (MSVC)
    find_package(FFTW3f CONFIG REQUIRED)
    target_link_libraries(lora_capture_probe PRIVATE FFTW3::fftw3f ${SDRPP_VOLK_TARGET} bcrypt)
    add_custom_command(TARGET lora_capture_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:FFTW3::fftw3f>
            $<TARGET_FILE_DIR:lora_capture_probe>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${SDRPP_VOLK_TARGET}>
            $<TARGET_FILE_DIR:lora_capture_probe>
    )
else()
    target_include_directories(lora_capture_probe PRIVATE ${LORA_FFTW3F_INCLUDE_DIRS})
    target_link_directories(lora_capture_probe PRIVATE ${LORA_FFTW3F_LIBRARY_DIRS})
    target_link_libraries(lora_capture_probe PRIVATE ${LORA_FFTW3F_LIBRARIES} ${SDRPP_VOLK_TARGET})
endif()
target_compile_options(lora_capture_probe PRIVATE ${SDRPP_COMPILER_FLAGS})
if (EXISTS "${CMAKE_SOURCE_DIR}/Meshtastic_EFL_int16.wav")
    add_test(NAME meshtastic_efl_iq_text
        COMMAND lora_capture_probe "${CMAKE_SOURCE_DIR}/Meshtastic_EFL_int16.wav" 0x2b 4 require)
endif()
