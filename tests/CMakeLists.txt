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
