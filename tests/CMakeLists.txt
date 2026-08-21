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
