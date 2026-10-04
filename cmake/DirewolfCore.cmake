# DirewolfCore.cmake - build the Dire Wolf modem and protocol core as a
# static library for embedding, from the sources in external/direwolf.
#
# What is left out on purpose, compared with the stand-alone direwolf target:
#   direwolf.c                 the program's main(), replaced by src/core/dw_embed.c
#   textcolor.c                stdout output, replaced by src/core/dw_textcolor.c
#   kissnet.c kissserial.c     KISS servers (TCP, serial, pseudo terminal)
#   kiss.c server.c            AGW server
#   igate.c nettnc.c           Internet gateway, network TNC channels
#   digipeater.c cdigipeater.c the digipeater
#   beacon.c                   Direwolf's own beacons (the host has its own)
#   aprs_tt.c tt_user.c        the APRStt gateway
#   dwgpsd.c waypoint.c        gpsd client and waypoint output (dwgps.c and
#                              dwgpsnmea.c stay: decode_aprs.c parses raw NMEA)
#   log.c mheard.c pfilter.c   log files, heard list, packet filters
#   dns_sd_*.c                 Bonjour/Avahi advertising of the KISS port
# The functions those modules export and that the core still references are
# provided as no-ops in src/core/dw_stubs.c.
#
# The Direwolf sources must carry patches/direwolf/*.patch; the fetch script
# applies them and this file checks for their marker.

set(DIREWOLF_DIR "${CMAKE_SOURCE_DIR}/external/direwolf" CACHE PATH
    "Directory of the (patched) Dire Wolf source tree")

if(NOT EXISTS "${DIREWOLF_DIR}/src/direwolf.h")
  message(FATAL_ERROR
    "Dire Wolf sources not found in ${DIREWOLF_DIR}.\n"
    "Run scripts/fetch_direwolf.sh (or point DIREWOLF_DIR at a checkout of "
    "tag 1.8 with patches/direwolf applied).")
endif()

file(STRINGS "${DIREWOLF_DIR}/src/tq.h" _dw_has_tq_term REGEX "tq_term")
if(NOT _dw_has_tq_term)
  message(FATAL_ERROR
    "The Dire Wolf sources in ${DIREWOLF_DIR} do not carry "
    "patches/direwolf/0001-embedded-host-shutdown.patch. "
    "Run scripts/fetch_direwolf.sh, or apply the patch by hand.")
endif()

# Version, read from the Direwolf tree so it cannot drift from the sources.
function(_dw_read_version var name default)
  set(${var} "${default}" PARENT_SCOPE)
  file(STRINGS "${DIREWOLF_DIR}/CMakeLists.txt" _line REGEX "^set\\(direwolf_VERSION_${name} ")
  if(_line)
    string(REGEX REPLACE ".*\"([0-9]+)\".*" "\\1" _v "${_line}")
    set(${var} "${_v}" PARENT_SCOPE)
  endif()
endfunction()
_dw_read_version(DW_CORE_VERSION_MAJOR MAJOR 1)
_dw_read_version(DW_CORE_VERSION_MINOR MINOR 8)
_dw_read_version(DW_CORE_VERSION_PATCH PATCH 0)
set(DW_CORE_VERSION "${DW_CORE_VERSION_MAJOR}.${DW_CORE_VERSION_MINOR}.${DW_CORE_VERSION_PATCH}")
message(STATUS "Embedding Dire Wolf ${DW_CORE_VERSION} from ${DIREWOLF_DIR}")

list(APPEND CMAKE_MODULE_PATH "${DIREWOLF_DIR}/cmake/modules")

include(CheckSymbolExists)
check_symbol_exists(strlcpy string.h HAVE_STRLCPY)
check_symbol_exists(strlcat string.h HAVE_STRLCAT)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(DW_SRC "${DIREWOLF_DIR}/src")

set(DW_CORE_SOURCES
  # AX.25 frames, FCS, addresses
  ${DW_SRC}/ax25_pad.c
  ${DW_SRC}/ax25_pad2.c
  ${DW_SRC}/fcs_calc.c
  # Demodulators and HDLC receive
  ${DW_SRC}/demod.c
  ${DW_SRC}/demod_afsk.c
  ${DW_SRC}/demod_psk.c
  ${DW_SRC}/demod_9600.c
  ${DW_SRC}/dsp.c
  ${DW_SRC}/hdlc_rec.c
  ${DW_SRC}/hdlc_rec2.c
  ${DW_SRC}/multi_modem.c
  ${DW_SRC}/rrbb.c
  ${DW_SRC}/audio_stats.c
  ${DW_SRC}/recv.c
  ${DW_SRC}/dlq.c
  # Forward error correction
  ${DW_SRC}/fx25_encode.c
  ${DW_SRC}/fx25_extract.c
  ${DW_SRC}/fx25_init.c
  ${DW_SRC}/fx25_rec.c
  ${DW_SRC}/fx25_send.c
  ${DW_SRC}/fx25_auto.c
  ${DW_SRC}/il2p_codec.c
  ${DW_SRC}/il2p_scramble.c
  ${DW_SRC}/il2p_rec.c
  ${DW_SRC}/il2p_payload.c
  ${DW_SRC}/il2p_init.c
  ${DW_SRC}/il2p_header.c
  ${DW_SRC}/il2p_send.c
  # Transmit side
  ${DW_SRC}/tq.c
  ${DW_SRC}/xmit.c
  ${DW_SRC}/hdlc_send.c
  ${DW_SRC}/gen_tone.c
  ${DW_SRC}/morse.c
  ${DW_SRC}/dtmf.c
  ${DW_SRC}/ptt.c
  ${DW_SRC}/gpio_common.c
  ${DW_SRC}/serial_port.c
  # Connected mode link layer (recv.c dispatches to it)
  ${DW_SRC}/ax25_link.c
  ${DW_SRC}/xid.c
  # Configuration file
  ${DW_SRC}/config.c
  ${DW_SRC}/latlong.c
  # APRS decoding, usable by the host
  ${DW_SRC}/decode_aprs.c
  ${DW_SRC}/encode_aprs.c
  ${DW_SRC}/symbols.c
  ${DW_SRC}/deviceid.c
  ${DW_SRC}/telemetry.c
  ${DW_SRC}/tt_text.c
  ${DW_SRC}/ais.c
  ${DW_SRC}/dedupe.c
  ${DW_SRC}/dwgpsnmea.c
  ${DW_SRC}/dwgps.c
  # Misc
  ${DW_SRC}/dtime_now.c
  ${DW_SRC}/dwsock.c
  ${DW_SRC}/kiss_frame.c
)

# The embedding layer.
set(DW_EMBED_SOURCES
  ${CMAKE_SOURCE_DIR}/src/core/dw_embed.c
  ${CMAKE_SOURCE_DIR}/src/core/dw_textcolor.c
  ${CMAKE_SOURCE_DIR}/src/core/dw_stubs.c
)

set(DW_CORE_DEFINITIONS
  COMPANY="wb2osz"
  APPLICATION_NAME="Dire Wolf"
  DW_DIREWOLF_VERSION="${DW_CORE_VERSION}"
  MAJOR_VERSION=${DW_CORE_VERSION_MAJOR}
  MINOR_VERSION=${DW_CORE_VERSION_MINOR}
  DW_EMBEDDED=1
)
set(DW_CORE_LIBRARIES Threads::Threads)
set(DW_CORE_INCLUDES_PRIVATE ${DIREWOLF_DIR}/external/geotranz)

if(HAVE_STRLCPY)
  list(APPEND DW_CORE_DEFINITIONS HAVE_STRLCPY)
endif()
if(HAVE_STRLCAT)
  list(APPEND DW_CORE_DEFINITIONS HAVE_STRLCAT)
endif()

# UTM / MGRS / USNG conversions used by config.c
file(GLOB DW_GEOTRANZ_SOURCES ${DIREWOLF_DIR}/external/geotranz/*.c)

# Replacement string functions
set(DW_MISC_SOURCES "")
set(DW_MISC_DIR ${DIREWOLF_DIR}/external/misc)
if(NOT HAVE_STRLCPY)
  list(APPEND DW_MISC_SOURCES ${DW_MISC_DIR}/strlcpy.c)
endif()
if(NOT HAVE_STRLCAT)
  list(APPEND DW_MISC_SOURCES ${DW_MISC_DIR}/strlcat.c)
endif()

# Platform audio back end and PTT helpers, same choices as Direwolf's build.
if(ANDROID)
  # No ALSA, no device files: audio through Oboe (fetched from GitHub at
  # configure time) and PTT through the host's USB code (ptt_android.c,
  # AndroidPtt.cpp, UsbPtt.java).  config.c gets USE_CM108 so it accepts
  # the CM108 keyword; the two cm108.c functions it needs are in
  # ptt_android.c.
  list(REMOVE_ITEM DW_CORE_SOURCES ${DW_SRC}/ptt.c)
  list(APPEND DW_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/audio_oboe.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ptt_android.c)
  list(APPEND DW_CORE_DEFINITIONS USE_CM108)
  include(FetchContent)
  set(OBOE_GIT_TAG "1.9.3" CACHE STRING "Oboe version fetched for the Android audio backend")
  FetchContent_Declare(oboe
    GIT_REPOSITORY https://github.com/google/oboe.git
    GIT_TAG ${OBOE_GIT_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(oboe)
  list(APPEND DW_CORE_LIBRARIES oboe log)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(ALSA REQUIRED)
  list(APPEND DW_CORE_SOURCES ${DW_SRC}/audio.c)
  list(APPEND DW_CORE_DEFINITIONS USE_ALSA)
  list(APPEND DW_CORE_LIBRARIES ${ALSA_LIBRARIES})
  list(APPEND DW_CORE_INCLUDES_PRIVATE ${ALSA_INCLUDE_DIRS})
  find_package(udev)
  if(UDEV_FOUND)
    list(APPEND DW_CORE_SOURCES ${DW_SRC}/cm108.c)
    list(APPEND DW_CORE_DEFINITIONS USE_CM108)
    list(APPEND DW_CORE_LIBRARIES ${UDEV_LIBRARIES})
    list(APPEND DW_CORE_INCLUDES_PRIVATE ${UDEV_INCLUDE_DIRS})
  endif()
  list(APPEND DW_CORE_LIBRARIES m rt)
elseif(WIN32)
  # The Dire Wolf sources use GCC extensions (__attribute__((hot)) and
  # friends) and POSIX headers, so cl.exe cannot compile them.  MinGW-w64
  # can, and so can clang-cl, the Clang compiler Visual Studio ships with
  # the "C++ Clang tools for Windows" component: MSVC ABI and runtime, so
  # the result links with the Qt MSVC binaries and with C++ built by cl.
  # build_all.bat configures with clang-cl for C and cl for C++.
  if(MSVC AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
      "The Dire Wolf core cannot be compiled by cl.exe (it uses GCC extensions). "
      "Configure with clang-cl for C: -DCMAKE_C_COMPILER=clang-cl "
      "(keep cl for C++ with -DCMAKE_CXX_COMPILER=cl). clang-cl comes with the "
      "'C++ Clang tools for Windows' component of the Visual Studio installer.")
  endif()
  list(APPEND DW_CORE_SOURCES ${DW_SRC}/audio_win.c ${DW_SRC}/cm108.c)
  list(APPEND DW_MISC_SOURCES
    ${DW_MISC_DIR}/strsep.c
    ${DW_MISC_DIR}/strtok_r.c
    ${DW_MISC_DIR}/strcasestr.c
    ${DW_MISC_DIR}/strlcpy.c
    ${DW_MISC_DIR}/strlcat.c)
  list(REMOVE_DUPLICATES DW_MISC_SOURCES)
  # GNU regex for the native Windows build: regex.c includes the other
  # translation units itself, so it is the only one compiled.
  set(DW_REGEX_SOURCES ${DIREWOLF_DIR}/external/regex/regex.c)
  set_source_files_properties(${DW_REGEX_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "bool=int;true=1;false=0;REGEX_STATIC")
  # HID access for the CM108 PTT, the hidapi copy that ships with Dire Wolf.
  set(DW_HIDAPI_SOURCES ${DIREWOLF_DIR}/external/hidapi/hid.c)
  set_source_files_properties(${DW_HIDAPI_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "bool=int;true=1;false=0;USE_HIDAPI_STATIC")
  list(APPEND DW_CORE_SOURCES ${DW_REGEX_SOURCES} ${DW_HIDAPI_SOURCES})
  list(APPEND DW_CORE_INCLUDES_PRIVATE ${DIREWOLF_DIR}/external/regex ${DIREWOLF_DIR}/external/hidapi)
  list(APPEND DW_CORE_DEFINITIONS USE_CM108 USE_REGEX_STATIC)
  list(APPEND DW_CORE_LIBRARIES winmm ws2_32 setupapi hid)
  if(MSVC)
    # <unistd.h> for the Microsoft runtime, see src/core/msvc/.
    list(APPEND DW_CORE_INCLUDES_PRIVATE ${CMAKE_SOURCE_DIR}/src/core/msvc)
  endif()
else()
  # macOS and the BSDs
  find_package(Portaudio REQUIRED)
  list(APPEND DW_CORE_SOURCES ${DW_SRC}/audio_portaudio.c ${DW_SRC}/cm108.c)
  list(APPEND DW_CORE_DEFINITIONS USE_PORTAUDIO)
  list(APPEND DW_CORE_LIBRARIES ${PORTAUDIO_LIBRARIES})
  list(APPEND DW_CORE_INCLUDES_PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
  find_package(hidapi)
  if(HIDAPI_FOUND)
    list(APPEND DW_CORE_DEFINITIONS USE_CM108)
    list(APPEND DW_CORE_LIBRARIES ${HIDAPI_LIBRARIES})
    list(APPEND DW_CORE_INCLUDES_PRIVATE ${HIDAPI_INCLUDE_DIRS})
  endif()
endif()

# Optional: PTT through Hamlib (PTT RIG ... in direwolf.conf)
if(NOT ANDROID)
  find_package(hamlib)
endif()
if(HAMLIB_FOUND)
  list(APPEND DW_CORE_DEFINITIONS USE_HAMLIB)
  list(APPEND DW_CORE_LIBRARIES ${HAMLIB_LIBRARIES})
  list(APPEND DW_CORE_INCLUDES_PRIVATE ${HAMLIB_INCLUDE_DIRS})
endif()

add_library(direwolf_core STATIC
  ${DW_CORE_SOURCES}
  ${DW_EMBED_SOURCES}
  ${DW_GEOTRANZ_SOURCES}
  ${DW_MISC_SOURCES}
)

target_include_directories(direwolf_core
  PUBLIC ${DW_SRC} ${CMAKE_SOURCE_DIR}/src/core
  PRIVATE ${DW_CORE_INCLUDES_PRIVATE}
)
if(ANDROID)
  target_link_libraries(direwolf_core PRIVATE oboe)
endif()
target_compile_definitions(direwolf_core PRIVATE ${DW_CORE_DEFINITIONS})
target_link_libraries(direwolf_core PUBLIC ${DW_CORE_LIBRARIES})

# Same optimisation flags as Direwolf's own build.  The forced include routes
# every exit() in the Direwolf sources to dw_embed_exit(), see dw_embed_shim.h.
# The shim is forced into the Dire Wolf translation units only; our own
# files in src/core include what they need themselves.
set(DW_THIRD_PARTY_SOURCES ${DW_CORE_SOURCES} ${DW_GEOTRANZ_SOURCES} ${DW_MISC_SOURCES})
list(REMOVE_ITEM DW_THIRD_PARTY_SOURCES
  ${CMAKE_SOURCE_DIR}/src/core/audio_oboe.cpp
  ${CMAKE_SOURCE_DIR}/src/core/ptt_android.c)
if(MSVC)
  target_compile_options(direwolf_core PRIVATE /W3)
  set_source_files_properties(${DW_THIRD_PARTY_SOURCES} PROPERTIES
    COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/src/core/dw_embed_shim.h;/w")
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # textcolor.h declares dw_printf with format(ms_printf), a GCC spelling
    # clang-cl does not know; the warning says nothing we can act on.
    target_compile_options(direwolf_core PRIVATE -Wno-ignored-attributes)
  endif()
else()
  target_compile_options(direwolf_core PRIVATE -Wall -Wvla -ffast-math -ftree-vectorize)
  if(NOT WIN32)
    target_compile_options(direwolf_core PRIVATE -D_GNU_SOURCE)
  endif()
  set_source_files_properties(${DW_THIRD_PARTY_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/src/core/dw_embed_shim.h;-w")
endif()

# Data files the decoders look for in ./data relative to the working
# directory: device identification from the destination address, and the
# newer overlay symbols.  Copied next to the binaries so tests and tools
# find them when run from the build directory; the application installs
# them beside its executable.
set(DW_DATA_FILES tocalls.yaml symbols-new.txt)
foreach(_f ${DW_DATA_FILES})
  if(EXISTS "${DIREWOLF_DIR}/data/${_f}")
    configure_file("${DIREWOLF_DIR}/data/${_f}" "${CMAKE_BINARY_DIR}/data/${_f}" COPYONLY)
  endif()
endforeach()

# Direwolf is GPL-2.0-or-later.  Anything that links direwolf_core is a
# derived work and must be distributed under the GPL.
