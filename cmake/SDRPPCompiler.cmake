# Keep project-specific flags here. Optimization and debug flags come from the
# selected CMake configuration and compiler toolchain.
if (MSVC)
    set(SDRPP_COMPILER_FLAGS /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SDRPP_COMPILER_FLAGS -Wno-unused-command-line-argument -Wno-deprecated-register)
else()
    set(SDRPP_COMPILER_FLAGS "")
endif()
set(SDRPP_MODULE_COMPILER_FLAGS "${SDRPP_COMPILER_FLAGS}")

# Apply project flags only to project targets, with C++ flags kept out of C.
function(sdrpp_set_compile_options target)
    set(_c_flags ${SDRPP_COMPILER_FLAGS})
    list(REMOVE_ITEM _c_flags /EHsc)
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${SDRPP_COMPILER_FLAGS}>"
        "$<$<COMPILE_LANGUAGE:C>:${_c_flags}>"
    )
    if (MSVC)
        target_compile_options(${target} PRIVATE /wd4996)
        target_compile_definitions(${target} PRIVATE _USE_MATH_DEFINES WINVER=0x0601 _WIN32_WINNT=0x0601)
    else()
        target_compile_options(${target} PRIVATE -Wno-deprecated-declarations)
    endif()
endfunction()
