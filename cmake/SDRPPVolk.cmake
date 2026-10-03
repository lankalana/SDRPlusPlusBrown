# GNU Radio VOLK: installed dependency by default, with an optional source fallback.
if (SDRPP_USE_BUNDLED_VOLK)
    include(FetchContent)

    set(SDRPP_PYTHON_VENV "${CMAKE_SOURCE_DIR}/.venv" CACHE PATH "Python virtual environment for build tools")
    if (WIN32)
        set(SDRPP_VENV_PYTHON "${SDRPP_PYTHON_VENV}/Scripts/python.exe")
    else()
        set(SDRPP_VENV_PYTHON "${SDRPP_PYTHON_VENV}/bin/python")
    endif()

    if (NOT EXISTS "${SDRPP_VENV_PYTHON}")
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
        message(STATUS "Creating Python virtual environment at ${SDRPP_PYTHON_VENV}")
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m venv "${SDRPP_PYTHON_VENV}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    set(Python3_EXECUTABLE "${SDRPP_VENV_PYTHON}" CACHE FILEPATH
        "Python interpreter used for SDR++ build tools" FORCE)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import mako"
        RESULT_VARIABLE SDRPP_MAKO_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if (NOT SDRPP_MAKO_RESULT EQUAL 0)
        message(STATUS "Installing Python build requirements into ${SDRPP_PYTHON_VENV}")
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m pip install --disable-pip-version-check mako
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    set(ENABLE_TESTING OFF)
    set(ENABLE_UTILITY_APPS OFF)
    set(ENABLE_MODTOOL OFF)
    set(ENABLE_PROFILING OFF)

    FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/gnuradio/volk.git
        GIT_TAG e73b4b442b162db2888e66a92ecbbf171e5497b8
        GIT_SUBMODULES cpu_features
    )

    FetchContent_MakeAvailable(volk)
    # VOLK is an implementation dependency. Keep its development files and
    # install rules out of SDR++ packages while retaining target dependencies.
    set_property(DIRECTORY "${volk_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    set(SDRPP_VOLK_TARGET volk)
elseif (MSVC)
    find_package(Volk CONFIG REQUIRED)
    if (TARGET Volk::volk)
        set(SDRPP_VOLK_TARGET Volk::volk)
    elseif (TARGET volk)
        set(SDRPP_VOLK_TARGET volk)
    else()
        message(FATAL_ERROR "The VOLK package did not provide Volk::volk or volk")
    endif()
else()
    set(SDRPP_VOLK_TARGET volk)
endif()
