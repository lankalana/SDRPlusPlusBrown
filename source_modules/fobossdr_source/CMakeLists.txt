cmake_minimum_required(VERSION 3.21)
project(fobossdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(fobossdr_source PRIVATE "${FOBOSSDR_ROOT}/lib/")
    target_include_directories(fobossdr_source PRIVATE "${FOBOSSDR_ROOT}/include/")
    target_link_libraries(fobossdr_source PRIVATE fobos)
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBFOBOS REQUIRED libfobos)

    target_include_directories(fobossdr_source PRIVATE ${LIBFOBOS_INCLUDE_DIRS})
    target_link_directories(fobossdr_source PRIVATE ${LIBFOBOS_LIBRARY_DIRS})
    target_link_libraries(fobossdr_source PRIVATE ${LIBFOBOS_LIBRARIES})
endif ()
