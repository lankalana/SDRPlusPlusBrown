cmake_minimum_required(VERSION 3.21)
project(hydrasdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    target_link_directories(hydrasdr_source PRIVATE "${HYDRASDR_ROOT}/lib/")

    target_include_directories(hydrasdr_source PUBLIC "${HYDRASDR_ROOT}/include/libhydrasdr")

    target_link_libraries(hydrasdr_source PRIVATE hydrasdr)
elseif (ANDROID)
    target_include_directories(hydrasdr_source PUBLIC
    /sdr-kit/${ANDROID_ABI}/include/libhydrasdr
    )

    target_link_libraries(hydrasdr_source PUBLIC
    /sdr-kit/${ANDROID_ABI}/lib/libusb1.0.so
    /sdr-kit/${ANDROID_ABI}/lib/libhydrasdr.so
)
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBHYDRASDR REQUIRED libhydrasdr)

    target_include_directories(hydrasdr_source PRIVATE ${LIBHYDRASDR_INCLUDE_DIRS})
    target_link_directories(hydrasdr_source PRIVATE ${LIBHYDRASDR_LIBRARY_DIRS})
    target_link_libraries(hydrasdr_source PRIVATE ${LIBHYDRASDR_LIBRARIES})

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(hydrasdr_source PRIVATE "/usr/local/include")
    endif()
endif ()
