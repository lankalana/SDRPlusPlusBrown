# Prepare a directly runnable development tree.
if (MSVC)
    add_custom_command(
        TARGET sdrpp
        POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdrpp>
            "$<TARGET_FILE_DIR:sdrpp>"

        COMMENT "Deploying SDR++ runtime dependencies"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    if (COPY_MSVC_REDISTRIBUTABLES)
        # Get the list of Visual C++ runtime DLLs
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP True)
        include(InstallRequiredSystemLibraries)

        # Copy the runtime DLLs into the development tree.
        foreach(DLL IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            add_custom_command(TARGET sdrpp POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:sdrpp>"
                VERBATIM
            )
        endforeach()
    endif ()

    add_custom_target(stage_runtime ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:sdrpp>/res"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/root/res"
            "$<TARGET_FILE_DIR:sdrpp>/res"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:sdrpp>/modules"
        COMMENT "Preparing SDR++ runtime directory"
        VERBATIM
    )
    get_property(_module_targets GLOBAL PROPERTY SDRPP_MODULE_TARGETS)
    set(_module_binaries)
    foreach(_target IN LISTS _module_targets)
        list(APPEND _module_binaries "$<TARGET_FILE_NAME:${_target}>")
    endforeach()
    add_custom_command(TARGET stage_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_ROOT=${CMAKE_BINARY_DIR}"
            "-DMODULE_DIR=$<TARGET_FILE_DIR:sdrpp>/modules"
            "-DENABLED_BINARIES=${_module_binaries}"
            -P "${CMAKE_SOURCE_DIR}/cmake/SDRPPPruneModules.cmake"
        VERBATIM
    )
    add_dependencies(stage_runtime sdrpp ${_module_targets})
else()
    add_custom_command(TARGET sdrpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:sdrpp_core>"
            "$<TARGET_FILE_DIR:sdrpp>"
        COMMENT "Deploying SDR++ core runtime"
        VERBATIM
    )
endif ()
