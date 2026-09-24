vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO greatscottgadgets/hackrf
  REF v2026.01.3
  SHA512 e8441421437513fee54c09bd783085daa934878a2e5a08a6958a64debf5eb2e9e19922509856f21209335678defc9e7ff3649d928e227f605948ee523308d47f
  HEAD_REF master
)
set(_installed "${CURRENT_INSTALLED_DIR}")
vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}/host/libhackrf"
  OPTIONS
    -DINSTALL_UDEV_RULES=OFF
    "-DLIBUSB_INCLUDE_DIR=${_installed}/include/libusb-1.0"
  OPTIONS_RELEASE
    "-DLIBUSB_LIBRARIES=${_installed}/lib/libusb-1.0.lib"
  OPTIONS_DEBUG
    "-DLIBUSB_LIBRARIES=${_installed}/debug/lib/libusb-1.0.lib"
)
vcpkg_cmake_install()

if(VCPKG_TARGET_IS_WINDOWS)
  file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib" "${CURRENT_PACKAGES_DIR}/debug/lib")
  if(EXISTS "${CURRENT_PACKAGES_DIR}/bin/hackrf.lib")
    file(RENAME "${CURRENT_PACKAGES_DIR}/bin/hackrf.lib" "${CURRENT_PACKAGES_DIR}/lib/hackrf.lib")
  endif()
  if(EXISTS "${CURRENT_PACKAGES_DIR}/debug/bin/hackrf.lib")
    file(RENAME "${CURRENT_PACKAGES_DIR}/debug/bin/hackrf.lib" "${CURRENT_PACKAGES_DIR}/debug/lib/hackrf.lib")
  endif()
endif()

configure_file("${CMAKE_CURRENT_LIST_DIR}/hackrf-config.cmake.in"
               "${CURRENT_PACKAGES_DIR}/share/hackrf/hackrf-config.cmake" @ONLY)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib/pkgconfig" "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib/cmake" "${CURRENT_PACKAGES_DIR}/debug/lib/cmake")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
