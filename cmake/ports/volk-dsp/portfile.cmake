vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO gnuradio/volk
    REF e73b4b442b162db2888e66a92ecbbf171e5497b8
    SHA512 528788a5754f821c0237eeb212728d433e4547b06241f4b0b545c3fa2d360bc5f7e58ec3f66887f4d65bfaa7cf33e7887842031250f3c9de93212ab759eaa847
)

# Keep code-generation tools in the port's host environment, not the application.
x_vcpkg_get_python_packages(
    PYTHON_VERSION 3
    OUT_PYTHON_VAR VOLK_PYTHON
    PACKAGES "mako==1.3.10" "markupsafe==3.0.3"
)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(VOLK_STATIC ON)
else()
    set(VOLK_STATIC OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        "-DPython3_EXECUTABLE=${VOLK_PYTHON}"
        "-DENABLE_STATIC_LIBS=${VOLK_STATIC}"
        -DENABLE_TESTING=OFF
        -DENABLE_UTILITY_APPS=OFF
        -DENABLE_MODTOOL=OFF
        -DENABLE_PROFILING=OFF
        -DENABLE_ORC=OFF
        -DVOLK_CPU_FEATURES=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME Volk CONFIG_PATH lib/cmake/volk)
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
