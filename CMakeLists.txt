cmake_minimum_required(VERSION 3.21)
if (POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
project(sdrpp LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SDRPP_MODULE_DEFAULTS "DEFAULT" CACHE STRING "Default module set: DEFAULT or MINIMAL")
set_property(CACHE SDRPP_MODULE_DEFAULTS PROPERTY STRINGS DEFAULT MINIMAL)
if (SDRPP_MODULE_DEFAULTS STREQUAL "MINIMAL")
    set(SDRPP_DEFAULT_MODULE OFF)
elseif (SDRPP_MODULE_DEFAULTS STREQUAL "DEFAULT")
    set(SDRPP_DEFAULT_MODULE ON)
else()
    message(FATAL_ERROR "SDRPP_MODULE_DEFAULTS must be DEFAULT or MINIMAL")
endif()


# Backends
option(OPT_BACKEND_GLFW "Use the GLFW backend" ON)
option(OPT_BACKEND_ANDROID "Use the Android backend" OFF)

# Compatibility Options
option(OPT_OVERRIDE_STD_FILESYSTEM "Use a local version of std::filesystem on systems that don't have it yet" OFF)

# Sources
option(OPT_BUILD_AIRSPY_SOURCE "Build Airspy Source Module (Dependencies: libairspy)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_AIRSPYHF_SOURCE "Build Airspy HF+ Source Module (Dependencies: libairspyhf)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_AUDIO_SOURCE "Build Audio Source Module (Dependencies: rtaudio)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_BADGESDR_SOURCE "Build BadgeSDR Source Module (Dependencies: libusb)" OFF)
option(OPT_BUILD_BLADERF_SOURCE "Build BladeRF Source Module (Dependencies: libbladeRF)" OFF)
option(OPT_BUILD_FILE_SOURCE "Wav file source" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_FOBOSSDR_SOURCE "Build FobosSDR Source Module (Dependencies: libfobos)" OFF)
option(OPT_BUILD_HACKRF_SOURCE "Build HackRF Source Module (Dependencies: libhackrf)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_HAROGIC_SOURCE "Build Harogic Source Module (Dependencies: htra_api)" OFF)
option(OPT_BUILD_HERMES_SOURCE "Build Hermes Source Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_HYDRASDR_SOURCE "Build HydraSDR Source Module (Dependencies: libhydrasdr)" OFF)
option(OPT_BUILD_KCSDR_SOURCE "Build KCSDR Source Module (Dependencies: libkcsdr)" OFF)
option(OPT_BUILD_DRAGONLABS_SOURCE "Build DragonLabs Source Module (Dependencies: libdlcr)" OFF)
option(OPT_BUILD_LIMESDR_SOURCE "Build LimeSDR Source Module (Dependencies: liblimesuite)" OFF)
option(OPT_BUILD_NETWORK_SOURCE "Build Network Source Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_PERSEUS_SOURCE "Build Perseus Source Module (Dependencies: libperseus-sdr)" OFF)
option(OPT_BUILD_PLUTOSDR_SOURCE "Build PlutoSDR Source Module (Dependencies: libiio, libad9361)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_RFNM_SOURCE "Build RFNM Source Module (Dependencies: librfnm)" OFF)
option(OPT_BUILD_RFSPACE_SOURCE "Build RFspace Source Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_RTL_SDR_SOURCE "Build RTL-SDR Source Module (Dependencies: librtlsdr)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_RTL_TCP_SOURCE "Build RTL-TCP Source Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_SDRPP_SERVER_SOURCE "Build SDR++ Server Source Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_SDRPLAY_SOURCE "Build SDRplay Source Module (Dependencies: libsdrplay)" OFF)
option(OPT_BUILD_SOAPY_SOURCE "Build SoapySDR Source Module (Dependencies: soapysdr)" OFF)
option(OPT_BUILD_SPECTRAN_SOURCE "Build Spectran Source Module (Dependencies: Aaronia RTSA Suite)" OFF)
option(OPT_BUILD_SPECTRAN_HTTP_SOURCE "Build Spectran HTTP Source Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_SPYSERVER_SOURCE "Build SpyServer Source Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_HL2_SOURCE "Build Hermes Lite 2 Source Module" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_USRP_SOURCE "Build USRP Source Module (libuhd)" OFF)
option(OPT_BUILD_KIWISDR_SOURCE "Build KiwiSDR (web) Source Module" ${SDRPP_DEFAULT_MODULE})

# Sinks
option(OPT_BUILD_ANDROID_AUDIO_SINK "Build Android Audio Sink Module (Dependencies: AAudio, only for android)" OFF)
option(OPT_BUILD_AUDIO_SINK "Build Audio Sink Module (Dependencies: rtaudio)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_NETWORK_SINK "Build Audio Sink Module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_MPEG_ADTS_SINK "Build MPEG ADTS Sink Module (lame will be used)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_NEW_PORTAUDIO_SINK "Build the new PortAudio Sink Module (Dependencies: portaudio)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_PORTAUDIO_SINK "Build PortAudio Sink Module (Dependencies: portaudio)" OFF)

# Decoders
option(OPT_BUILD_ATV_DECODER "Build ATV decoder (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_DAB_DECODER "Build the DAB/DAB+ decoder (no dependencies required)" OFF)
option(OPT_BUILD_FALCON9_DECODER "Build the falcon9 live decoder (Dependencies: ffplay)" OFF)
option(OPT_BUILD_KG_SSTV_DECODER "Build the M17 decoder module (no dependencies required)" OFF)
option(OPT_BUILD_M17_DECODER "Build the M17 decoder module (Dependencies: codec2)" OFF)
option(OPT_BUILD_CH_EXTRAVHF_DECODER "Build the extra VHF decoder module" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_CH_TETRA_DEMODULATOR "Build the tetra demodulator" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_FT8_DECODER "Build the FT8 decoder module" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_ADSB_DECODER "Build the ADS-B 1090ES decoder module" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_METEOR_DEMODULATOR "Build the meteor demodulator module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_PAGER_DECODER "Build the pager decoder module (no dependencies required)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_MESHTASTIC_DECODER "Build the receive-only Meshtastic EFL decoder module (Dependencies: system crypto provider)" OFF)
option(OPT_BUILD_MESHCORE_DECODER "Build the receive-only MeshCore decoder module (Dependencies: system crypto provider)" OFF)
option(OPT_BUILD_RADIO "Main audio modulation decoder (AM, FM, SSB, etc...)" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_RYFI_DECODER "RyFi data link decoder" OFF)
option(OPT_BUILD_VOR_RECEIVER "VOR beacon receiver" OFF)
option(OPT_BUILD_WEATHER_SAT_DECODER "Build the HRPT decoder module (no dependencies required)" OFF)

# Misc
option(OPT_BUILD_DISCORD_PRESENCE "Build the Discord Rich Presence module" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_FREQUENCY_MANAGER "Build the Frequency Manager module" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_IQ_EXPORTER "Build the IQ Exporter module" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_RECORDER "Audio and baseband recorder" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_RIGCTL_CLIENT "Rigctl client to make SDR++ act as a panadapter" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_RIGCTL_SERVER "Rigctl backend for controlling SDR++ with software like gpredict" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_TCI_SERVER "TCI backend for controlling SDR++ with sunsdr and similar software" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_SCANNER "Frequency scanner" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_SCHEDULER "Build the scheduler" OFF)
option(OPT_BUILD_NOISE_REDUCTION_LOGMMSE "Build LOGMMSE noise reduction" ${SDRPP_DEFAULT_MODULE})
option(OPT_BUILD_WEB_SDR_VIEW "Build Web SDR view" ${SDRPP_DEFAULT_MODULE})

# Other options
option(USE_INTERNAL_LIBCORRECT "Use an internal version of libcorrect" ON)
option(USE_BUNDLE_DEFAULTS "Set the default resource and module directories to the right ones for a MacOS .app" OFF)
option(COPY_MSVC_REDISTRIBUTABLES "Copy over the Visual C++ Redistributable" OFF)
option(BUILD_TESTS "Build test suite" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

if (MSVC)
    set(_SDRPP_BUNDLED_VOLK_DEFAULT ON)
else()
    set(_SDRPP_BUNDLED_VOLK_DEFAULT OFF)
endif()
option(SDRPP_USE_BUNDLED_VOLK "Build the pinned VOLK dependency from source" ${_SDRPP_BUNDLED_VOLK_DEFAULT})

if (WIN32)
    set(POTHOSSDR_ROOT "C:/Program Files/PothosSDR" CACHE PATH "PothosSDR installation directory")
    set(SDRPLAY_ROOT "C:/Program Files/SDRplay/API" CACHE PATH "SDRplay API installation directory")
    set(CODEC2_ROOT "C:/Program Files/codec2" CACHE PATH "Codec2 installation directory")
    set(HYDRASDR_ROOT "C:/Program Files/hydrasdr-host" CACHE PATH "HydraSDR installation directory")
    set(RFNM_ROOT "C:/Program Files/RFNM" CACHE PATH "RFNM installation directory")
    set(FOBOSSDR_ROOT "C:/Program Files/RigExpert/Fobos" CACHE PATH "FobosSDR installation directory")
    set(SPECTRAN_ROOT "C:/Program Files/Aaronia AG/Aaronia RTSA-Suite PRO/sdk" CACHE PATH "Aaronia Spectran SDK directory")
    set(UHD_DIR "C:/Program Files/uhd" CACHE PATH "UHD installation directory")

    set(SDRPP_RUNTIME_INSTALL_DIR ".")
    set(SDRPP_MODULE_INSTALL_DIR "modules")
    set(SDRPP_RESOURCE_INSTALL_DIR "res")
else()
    set(SDRPP_RUNTIME_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
    set(SDRPP_MODULE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/sdrpp/plugins")
    set(SDRPP_RESOURCE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/sdrpp")
endif()

# Module cmake path
set(SDRPP_MODULE_CMAKE "${CMAKE_SOURCE_DIR}/sdrpp_module.cmake")

# Root source folder
set(SDRPP_CORE_ROOT "${CMAKE_SOURCE_DIR}/core/src/")

# Keep project-specific flags here. Optimization and debug flags come from the
# selected CMake configuration and compiler toolchain.
if (MSVC)
    set(SDRPP_COMPILER_FLAGS /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SDRPP_COMPILER_FLAGS -Wno-unused-command-line-argument -Wno-deprecated-register)
else()
    set(SDRPP_COMPILER_FLAGS)
endif()
set(SDRPP_MODULE_COMPILER_FLAGS ${SDRPP_COMPILER_FLAGS})

if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES WINVER=0x0601 _WIN32_WINNT=0x0601)
endif()

# Set a default install prefix
if (UNIX AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "..." FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "..." FORCE)
    endif()
endif()

# Configure toolchain for android
if (ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate"
    )
endif (ANDROID)

# Create uninstall target
configure_file(${CMAKE_SOURCE_DIR}/cmake_uninstall.cmake ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake @ONLY)
add_custom_target(uninstall ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

# GNU Radio VOLK. Unix and Android builds keep using their existing system/SDK
# dependency unless bundled VOLK is explicitly requested.
if (SDRPP_USE_BUNDLED_VOLK)
    include(FetchContent)

    set(SDRPP_PYTHON_VENV "${CMAKE_SOURCE_DIR}/.venv")
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

# Core of SDR++
add_subdirectory("core")

# Tests are added conditionally
if(BUILD_TESTS)
    enable_testing()
    include(CTest)
    add_subdirectory("tests")
endif()

function(sdrpp_add_optional_subdirectory option_name directory)
    if (${option_name})
        add_subdirectory("${directory}")
    endif()
endfunction()

# Source modules
sdrpp_add_optional_subdirectory(OPT_BUILD_AIRSPY_SOURCE source_modules/airspy_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_AIRSPYHF_SOURCE source_modules/airspyhf_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_AUDIO_SOURCE source_modules/audio_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_BADGESDR_SOURCE source_modules/badgesdr_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_BLADERF_SOURCE source_modules/bladerf_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_FILE_SOURCE source_modules/file_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_FOBOSSDR_SOURCE source_modules/fobossdr_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_HACKRF_SOURCE source_modules/hackrf_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_HAROGIC_SOURCE source_modules/harogic_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_HERMES_SOURCE source_modules/hermes_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_HYDRASDR_SOURCE source_modules/hydrasdr_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_DRAGONLABS_SOURCE source_modules/dragonlabs_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_KCSDR_SOURCE source_modules/kcsdr_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_LIMESDR_SOURCE source_modules/limesdr_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_NETWORK_SOURCE source_modules/network_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_PERSEUS_SOURCE source_modules/perseus_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_PLUTOSDR_SOURCE source_modules/plutosdr_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_RFNM_SOURCE source_modules/rfnm_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_RFSPACE_SOURCE source_modules/rfspace_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_RTL_SDR_SOURCE source_modules/rtl_sdr_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_RTL_TCP_SOURCE source_modules/rtl_tcp_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_SDDC_SOURCE source_modules/sddc_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_SDRPP_SERVER_SOURCE source_modules/sdrpp_server_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_SDRPLAY_SOURCE source_modules/sdrplay_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_SOAPY_SOURCE source_modules/soapy_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_SPECTRAN_SOURCE source_modules/spectran_source)

if (OPT_BUILD_SPECTRAN_HTTP_SOURCE)
    if (MSVC)
    else()
        add_subdirectory("source_modules/spectran_http_source")
    endif()
endif (OPT_BUILD_SPECTRAN_HTTP_SOURCE)

sdrpp_add_optional_subdirectory(OPT_BUILD_SPYSERVER_SOURCE source_modules/spyserver_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_HL2_SOURCE source_modules/hl2_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_USRP_SOURCE source_modules/usrp_source)
sdrpp_add_optional_subdirectory(OPT_BUILD_KIWISDR_SOURCE source_modules/kiwisdr_source)


# Sink modules
sdrpp_add_optional_subdirectory(OPT_BUILD_ANDROID_AUDIO_SINK sink_modules/android_audio_sink)

if (OPT_BUILD_AUDIO_SINK)
add_subdirectory("sink_modules/audio_sink")
# same as audio_sink but with mic enabled
add_subdirectory("sink_modules/brown_audio_sink")
endif (OPT_BUILD_AUDIO_SINK)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_subdirectory("sink_modules/linux_pulseaudio_sink")
endif ()

if(APPLE)
    add_subdirectory("sink_modules/macos_coreaudio_sink")
endif()


sdrpp_add_optional_subdirectory(OPT_BUILD_PORTAUDIO_SINK sink_modules/portaudio_sink)
sdrpp_add_optional_subdirectory(OPT_BUILD_NETWORK_SINK sink_modules/network_sink)
sdrpp_add_optional_subdirectory(OPT_BUILD_MPEG_ADTS_SINK sink_modules/mpeg_adts_sink)

if (NOT ANDROID)
    if (OPT_BUILD_NEW_PORTAUDIO_SINK)
    add_subdirectory("sink_modules/new_portaudio_sink")
    endif (OPT_BUILD_NEW_PORTAUDIO_SINK)
endif ()

# Null audio sink (always built, no deps)
add_subdirectory("sink_modules/null_audio_sink")


# Decoders
sdrpp_add_optional_subdirectory(OPT_BUILD_ATV_DECODER decoder_modules/atv_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_DAB_DECODER decoder_modules/dab_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_FALCON9_DECODER decoder_modules/falcon9_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_KG_SSTV_DECODER decoder_modules/kg_sstv_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_M17_DECODER decoder_modules/m17_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_CH_EXTRAVHF_DECODER decoder_modules/ch_extravhf_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_CH_TETRA_DEMODULATOR decoder_modules/ch_tetra_demodulator)
sdrpp_add_optional_subdirectory(OPT_BUILD_FT8_DECODER decoder_modules/ft8_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_ADSB_DECODER decoder_modules/adsb_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_METEOR_DEMODULATOR decoder_modules/meteor_demodulator)
sdrpp_add_optional_subdirectory(OPT_BUILD_PAGER_DECODER decoder_modules/pager_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_MESHTASTIC_DECODER decoder_modules/meshtastic_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_MESHCORE_DECODER decoder_modules/meshcore_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_RADIO decoder_modules/radio)
sdrpp_add_optional_subdirectory(OPT_BUILD_RYFI_DECODER decoder_modules/ryfi_decoder)
sdrpp_add_optional_subdirectory(OPT_BUILD_VOR_RECEIVER decoder_modules/vor_receiver)
sdrpp_add_optional_subdirectory(OPT_BUILD_WEATHER_SAT_DECODER decoder_modules/weather_sat_decoder)


# Misc
sdrpp_add_optional_subdirectory(OPT_BUILD_DISCORD_PRESENCE misc_modules/discord_integration)
sdrpp_add_optional_subdirectory(OPT_BUILD_FREQUENCY_MANAGER misc_modules/frequency_manager)
sdrpp_add_optional_subdirectory(OPT_BUILD_IQ_EXPORTER misc_modules/iq_exporter)
sdrpp_add_optional_subdirectory(OPT_BUILD_RECORDER misc_modules/recorder)
sdrpp_add_optional_subdirectory(OPT_BUILD_RIGCTL_CLIENT misc_modules/rigctl_client)
sdrpp_add_optional_subdirectory(OPT_BUILD_RIGCTL_SERVER misc_modules/rigctl_server)
sdrpp_add_optional_subdirectory(OPT_BUILD_WEB_SDR_VIEW misc_modules/websdr_view)

if (OPT_BUILD_TCI_SERVER)
    if (MSVC)
        message(WARNING "TCI Server is not supported on Windows, skipping")
    elseif(ANDROID)
        message(WARNING "TCI Server is not supported on android, skipping")
#    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#        message(WARNING "TCI Server is not supported on macos, skipping")
    else()
        add_subdirectory("misc_modules/tci_server")
    endif()
endif (OPT_BUILD_TCI_SERVER)

add_subdirectory("misc_modules/reports_monitor")

sdrpp_add_optional_subdirectory(OPT_BUILD_SCANNER misc_modules/scanner)
sdrpp_add_optional_subdirectory(OPT_BUILD_SCHEDULER misc_modules/scheduler)
sdrpp_add_optional_subdirectory(OPT_BUILD_NOISE_REDUCTION_LOGMMSE misc_modules/noise_reduction_logmmse)

set(SDRPP_SOURCES "src/main.cpp")

if(WIN32)
    list(APPEND SDRPP_SOURCES "win32/resources.rc")
endif()

if(APPLE)
    list(APPEND SDRPP_SOURCES "src/macos_init.m")
endif()

add_executable(sdrpp ${SDRPP_SOURCES})

set(SDRPP_LIBS "sdrpp_core")

if(APPLE)
    list(APPEND SDRPP_LIBS "-framework Foundation")
    list(APPEND SDRPP_LIBS "-framework AVFoundation")
    list(APPEND SDRPP_LIBS "-framework Accelerate")
    set_source_files_properties("src/macos_init.m" PROPERTIES COMPILE_FLAGS "-std=c99")
endif()

if(NOT APPLE AND NOT WIN32 AND NOT ANDROID)
    list(APPEND SDRPP_LIBS orc-0.4)
endif()



target_link_libraries(sdrpp PRIVATE ${SDRPP_LIBS})

# Compiler arguments
target_compile_options(sdrpp PRIVATE ${SDRPP_COMPILER_FLAGS})

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
    add_dependencies(stage_runtime sdrpp)
else()
    add_custom_command(TARGET sdrpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:sdrpp_core>"
            "$<TARGET_FILE_DIR:sdrpp>"
        COMMENT "Deploying SDR++ core runtime"
        VERBATIM
    )
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    target_link_libraries(sdrpp PUBLIC pthread)
endif ()

# cmake .. "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_SCANNER=ON -DOPT_BUILD_SCHEDULER=ON -DOPT_BUILD_USRP_SOURCE=ON -DOPT_BUILD_PAGER_DECODER=ON

# Create module cmake file
configure_file(${CMAKE_SOURCE_DIR}/sdrpp_module.cmake ${CMAKE_CURRENT_BINARY_DIR}/sdrpp_module.cmake @ONLY)

# Install directives
if (WIN32)
    install(TARGETS sdrpp
        RUNTIME_DEPENDENCY_SET sdrpp_runtime_dependencies
        RUNTIME DESTINATION "${SDRPP_RUNTIME_INSTALL_DIR}"
    )
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/root/res/" DESTINATION "${SDRPP_RESOURCE_INSTALL_DIR}")

    set(SDRPP_WINDOWS_RUNTIME_DIRS
        "${CMAKE_BINARY_DIR}/$<CONFIG>"
        "${POTHOSSDR_ROOT}/bin"
        "${UHD_DIR}/bin"
        "${HYDRASDR_ROOT}/bin"
        "${RFNM_ROOT}/bin"
        "${CODEC2_ROOT}/lib"
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

# Create headers target

