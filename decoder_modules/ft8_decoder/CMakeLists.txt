cmake_minimum_required(VERSION 3.21)
project(ft8_decoder)


file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

if (ANDROID)
    target_link_libraries(ft8_decoder PRIVATE log)
endif()

target_include_directories(ft8_decoder PRIVATE "src/")

if(BUILD_TESTS AND NOT ANDROID)
    if(NOT TARGET Catch2::Catch2)
        find_package(Catch2 3 REQUIRED CONFIG)
    endif()
    list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")

    set(FT8_DECODER_TEST_SRC ${SRC})
    list(REMOVE_ITEM FT8_DECODER_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

    add_executable(ft8_decoder_tests
        tests/ft8_decoder_tests.cpp
        "${CMAKE_SOURCE_DIR}/tests/support/test_main.cpp"
        ${FT8_DECODER_TEST_SRC}
    )
    target_include_directories(ft8_decoder_tests PRIVATE
        "src/"
        "${CMAKE_SOURCE_DIR}/tests"
    )
    target_link_libraries(ft8_decoder_tests PRIVATE
        sdrpp_core
        Catch2::Catch2
    )
    target_compile_options(ft8_decoder_tests PRIVATE ${SDRPP_COMPILER_FLAGS})
    target_compile_definitions(ft8_decoder_tests PRIVATE
        SDRPP_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-tmp"
        FT8_TEST_WAV_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_files/ft8.wav"
        FT4_TEST_WAV_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_files/ft4.wav"
    )
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-tmp")

    if(MSVC)
        set_target_properties(ft8_decoder_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
        )
        add_custom_command(TARGET ft8_decoder_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:ft8_decoder_tests>
                $<TARGET_FILE_DIR:ft8_decoder_tests>
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()

    include(Catch OPTIONAL RESULT_VARIABLE FT8_CATCH_MODULE)
    if(FT8_CATCH_MODULE)
        catch_discover_tests(ft8_decoder_tests
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            PROPERTIES TIMEOUT 120
        )
    else()
        add_test(NAME ft8_decoder_tests COMMAND ft8_decoder_tests)
        set_tests_properties(ft8_decoder_tests PROPERTIES TIMEOUT 120)
    endif()
endif()
