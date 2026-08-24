# Unit / characterization test suite for core/src.
#
# The suite links against sdrpp_core so that every implementation living in
# core/src (utils, config, module_com, ...) is available, while the header-only
# DSP framework is compiled directly into the test binary.

find_package(Catch2 3 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")

# Test sources are globbed on purpose: adding a test file should not require
# touching the build. CONFIGURE_DEPENDS makes the generator re-glob on build.
file(GLOB_RECURSE SDRPP_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dsp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp"
)

add_executable(sdrpp_core_tests ${SDRPP_TEST_SOURCES})

target_include_directories(sdrpp_core_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/src"
)

target_link_libraries(sdrpp_core_tests PRIVATE
    sdrpp_core
    Catch2::Catch2
)

target_compile_options(sdrpp_core_tests PRIVATE ${SDRPP_COMPILER_FLAGS})

# Tests write scratch files (config, wav, riff) into this directory.
target_compile_definitions(sdrpp_core_tests PRIVATE
    SDRPP_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp"
)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp")

if (MSVC)
    set_target_properties(sdrpp_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
    add_custom_command(TARGET sdrpp_core_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdrpp_core_tests>
            $<TARGET_FILE_DIR:sdrpp_core_tests>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

# Register the tests with CTest. catch_discover_tests gives one CTest entry per
# Catch2 TEST_CASE, which makes failures readable in CI; fall back to a single
# entry when the Catch module isn't available.
include(Catch OPTIONAL RESULT_VARIABLE SDRPP_CATCH_MODULE)
if (SDRPP_CATCH_MODULE)
    catch_discover_tests(sdrpp_core_tests
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PROPERTIES TIMEOUT 120
    )
else()
    add_test(NAME sdrpp_core_tests COMMAND sdrpp_core_tests)
    set_tests_properties(sdrpp_core_tests PROPERTIES TIMEOUT 600)
endif()
