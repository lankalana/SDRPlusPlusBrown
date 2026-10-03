# Install directives
if (WIN32)
    install(TARGETS sdrpp
        RUNTIME_DEPENDENCY_SET sdrpp_runtime_dependencies
        RUNTIME DESTINATION "${SDRPP_RUNTIME_INSTALL_DIR}"
    )
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/root/res/" DESTINATION "${SDRPP_RESOURCE_INSTALL_DIR}")

    set(SDRPP_WINDOWS_RUNTIME_DIRS
        "${CMAKE_BINARY_DIR}/$<CONFIG>"
        "${SDRPLAY_ROOT}/x64"
    )
    install(RUNTIME_DEPENDENCY_SET sdrpp_runtime_dependencies
        DIRECTORIES ${SDRPP_WINDOWS_RUNTIME_DIRS}
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" "[Mm][Ss][Vv][Cc][Rr][0-9]+\\.dll"
        POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
        RUNTIME DESTINATION "${SDRPP_RUNTIME_INSTALL_DIR}"
    )
    if (COPY_MSVC_REDISTRIBUTABLES)
        install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION "${SDRPP_RUNTIME_INSTALL_DIR}")
    endif()
else()
    install(TARGETS sdrpp RUNTIME DESTINATION "${SDRPP_RUNTIME_INSTALL_DIR}")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/root/res/" DESTINATION "${SDRPP_RESOURCE_INSTALL_DIR}")
endif()
configure_file(${CMAKE_SOURCE_DIR}/sdrpp.desktop ${CMAKE_CURRENT_BINARY_DIR}/sdrpp.desktop @ONLY)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sdrpp.desktop DESTINATION share/applications)
endif ()

