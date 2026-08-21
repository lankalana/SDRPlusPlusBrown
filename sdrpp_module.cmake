# Get needed values depending on if this is in-tree or out-of-tree
if (NOT SDRPP_CORE_ROOT)
    set(SDRPP_CORE_ROOT "@SDRPP_CORE_ROOT@")
endif ()
if (NOT SDRPP_MODULE_COMPILER_FLAGS)
    set(SDRPP_MODULE_COMPILER_FLAGS @SDRPP_MODULE_COMPILER_FLAGS@)
endif ()

# Created shared lib and link to core
add_library(${PROJECT_NAME} SHARED ${SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE sdrpp_core)
target_include_directories(${PROJECT_NAME} PRIVATE "${SDRPP_CORE_ROOT}/src/")
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
if(MSVC)
    add_compile_options(/wd4996)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/modules"
    )
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>
            "${CMAKE_BINARY_DIR}/$<CONFIG>"

        COMMENT "Deploying ${PROJECT_NAME} runtime dependencies"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
else()
    add_compile_options(-Wno-deprecated-declarations)
endif()
# Set compile arguments; avoid applying C++-only flags to C sources.
set(_sdrpp_module_c_flags ${SDRPP_MODULE_COMPILER_FLAGS})
list(REMOVE_ITEM _sdrpp_module_c_flags -std=c++17 /std:c++17 /EHsc)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${SDRPP_MODULE_COMPILER_FLAGS}>
    $<$<COMPILE_LANGUAGE:C>:${_sdrpp_module_c_flags}>
)

# Install directives
if (WIN32)
    install(TARGETS ${PROJECT_NAME}
        RUNTIME_DEPENDENCY_SET sdrpp_runtime_dependencies
        RUNTIME DESTINATION "${SDRPP_MODULE_INSTALL_DIR}"
    )
else()
    install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION "${SDRPP_MODULE_INSTALL_DIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
