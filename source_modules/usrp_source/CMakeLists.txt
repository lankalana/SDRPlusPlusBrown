cmake_minimum_required(VERSION 3.21)
project(usrp_source)

file(GLOB SRC "src/*.cpp")

# 1. Initialize the module first
include(${SDRPP_MODULE_CMAKE})

# 2. Windows Environment
if (MSVC)
    # Source-built UHD (see .github/workflows/build_all.yml). Override with -DUHD_DIR=...
    find_path(UHD_INCLUDE_DIR NAMES uhd/version.hpp HINTS "${UHD_DIR}/include" REQUIRED)
    find_library(UHD_LIBRARY NAMES uhd HINTS "${UHD_DIR}/lib" REQUIRED)
    find_file(UHD_RUNTIME NAMES uhd.dll HINTS "${UHD_DIR}/bin" REQUIRED)
    add_library(UHD::uhd SHARED IMPORTED)
    set_target_properties(UHD::uhd PROPERTIES
        IMPORTED_IMPLIB "${UHD_LIBRARY}"
        IMPORTED_LOCATION "${UHD_RUNTIME}"
        INTERFACE_INCLUDE_DIRECTORIES "${UHD_INCLUDE_DIR}"
    )
    target_link_libraries(usrp_source PRIVATE UHD::uhd)

    # UHD public headers include boost headers; vcpkg toolchain provides them,
    # but link boost explicitly to satisfy MSVC auto-linking on boost_thread.
    find_package(Boost REQUIRED)
    target_include_directories(usrp_source PRIVATE ${Boost_INCLUDE_DIRS})

# 3. Unix Environment (macOS, Linux)
else ()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUHD REQUIRED uhd)

    # Clean up the libraries list to strip out broken plain boost strings exported by the .pc file
    set(CLEANED_UHD_LIBS "")
    foreach(lib ${LIBUHD_LIBRARIES})
        if (NOT lib MATCHES "^boost_")
            list(APPEND CLEANED_UHD_LIBS ${lib})
        endif()
    endforeach()

    target_include_directories(usrp_source PRIVATE ${LIBUHD_INCLUDE_DIRS})
    target_link_directories(usrp_source PRIVATE ${LIBUHD_LIBRARY_DIRS})
    
    # Link with the cleaned list
    target_link_libraries(usrp_source PRIVATE ${CLEANED_UHD_LIBS})

    # Find the proper Boost components natively matching the system layout
    # (boost_system is header-only since Boost 1.69 and no longer ships a
    # library/target, so only link it when CMake exposes the target)
    find_package(Boost REQUIRED)
    if (Boost_FOUND)
        target_include_directories(usrp_source PRIVATE ${Boost_INCLUDE_DIRS})
        if (TARGET Boost::system)
            target_link_libraries(usrp_source PRIVATE Boost::system)
        endif ()
        if (TARGET Boost::thread)
            target_link_libraries(usrp_source PRIVATE Boost::thread)
        endif ()
    endif ()
endif ()
