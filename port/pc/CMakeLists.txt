# ---------------------------------------------------------------------------
# The desktop simulator
#
#     cmake -S port/pc -B build-sim -G Ninja
#     cmake --build build-sim
#     ./build-sim/espp4-sim
#
# Builds the same src/ the board does, against port/pc/ instead of port/esp/:
# an SDL window where the MIPI-DSI panel is, can::CanuCan where the TWAI
# controller is, a directory of files where NVS is.
#
# LVGL comes from managed_components/lvgl__lvgl when it is there -- the very
# source the board builds, which is the point -- and is fetched at the pinned
# version when it is not. managed_components/ is resolved by the ESP-IDF
# component manager and not tracked, so a checkout that has never run
# `idf.py reconfigure` takes the second path.
#
# Needs, beyond a C++23 compiler: libsdl2-dev, libpaho-mqttpp-dev (the cloud
# client's desktop transport) and lv_font_conv for the Kanardia fonts
# (npm install --prefix tools lv_font_conv).
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

project(espp4_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)          # can::CanuCan uses std::print
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # ThorVG rasterises in software; an unoptimised build draws at a few frames
    # a second and tells you nothing about the real thing.
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

get_filename_component(KANARDIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${KANARDIA_ROOT}/cmake/KanardiaSources.cmake")

# ---------------------------------------------------------------------------
# SDL2
# ---------------------------------------------------------------------------

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
endif()

# ---------------------------------------------------------------------------
# Eclipse Paho, the MQTT client
# ---------------------------------------------------------------------------
#
# What port/pc/MqttPortPaho.cpp is app::MqttPort on, the way the board's port
# is esp-mqtt. Debian ships no CMake config for it, so this is a plain header
# and library search.
#
# Both halves are needed, and libpaho-mqttpp-dev does not pull the other one
# in: mqtt/async_client.h includes MQTTAsync.h, and enough of the C++ API is
# inline that the C library lands on the link line as well. So the C++ headers
# are there and unusable until the C package is installed too.

find_path(PAHO_MQTTPP_INCLUDE_DIR mqtt/async_client.h)
find_path(PAHO_MQTTC_INCLUDE_DIR MQTTAsync.h)
find_library(PAHO_MQTTPP_LIBRARY NAMES paho-mqttpp3)
# The SSL build first: it is what the packaged C++ library is linked against,
# and "ssl://" in MqttPortPaho.cpp needs nothing else the day the broker wants
# it. paho-mqtt3a is the same client without TLS.
find_library(PAHO_MQTTC_LIBRARY NAMES paho-mqtt3as paho-mqtt3a)

if(NOT PAHO_MQTTPP_INCLUDE_DIR OR NOT PAHO_MQTTC_INCLUDE_DIR
        OR NOT PAHO_MQTTPP_LIBRARY OR NOT PAHO_MQTTC_LIBRARY)
    message(FATAL_ERROR
        "Eclipse Paho MQTT not found. Install both halves with:\n"
        "    sudo apt install libpaho-mqtt-dev libpaho-mqttpp-dev")
endif()

message(STATUS "Paho:     ${PAHO_MQTTPP_LIBRARY}")

# ---------------------------------------------------------------------------
# LVGL, and the C++ binding over it
# ---------------------------------------------------------------------------

set(MANAGED "${KANARDIA_ROOT}/managed_components")

set(LVGL_SRC_DIR "${MANAGED}/lvgl__lvgl" CACHE PATH "LVGL source tree")
set(LVGL_CPP_DIR "${MANAGED}/lvgl_cpp"   CACHE PATH "lvgl_cpp source tree")

if(NOT EXISTS "${LVGL_SRC_DIR}/lvgl.h" OR NOT EXISTS "${LVGL_CPP_DIR}/lvgl_cpp.h")
    # Same versions main/idf_component.yml pins. Keep the two in step.
    include(FetchContent)
    if(NOT EXISTS "${LVGL_SRC_DIR}/lvgl.h")
        FetchContent_Declare(lvgl_fetched
            GIT_REPOSITORY https://github.com/lvgl/lvgl.git
            GIT_TAG        v9.5.0
            GIT_SHALLOW    TRUE)
        FetchContent_Populate(lvgl_fetched)
        set(LVGL_SRC_DIR "${lvgl_fetched_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${LVGL_CPP_DIR}/lvgl_cpp.h")
        FetchContent_Declare(lvgl_cpp_fetched
            GIT_REPOSITORY https://github.com/pedapudi/lvgl_cpp.git
            GIT_TAG        main
            GIT_SHALLOW    TRUE)
        FetchContent_Populate(lvgl_cpp_fetched)
        set(LVGL_CPP_DIR "${lvgl_cpp_fetched_SOURCE_DIR}")
    endif()
endif()

message(STATUS "LVGL:     ${LVGL_SRC_DIR}")
message(STATUS "lvgl_cpp: ${LVGL_CPP_DIR}")

# lv_conf.h is ours -- see the header of port/pc/lv_conf.h for what differs
# from the template and why. FORCE, because LVGL caches it and a stale entry
# from an earlier configure would silently win.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_SRC_DIR}" lvgl_build)

# LVGL's own SDL driver compiles <SDL2/SDL.h> and calls into it; nothing in
# LVGL's CMake links SDL, so it is done here.
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

# lvgl_cpp's own CMakeLists.txt is either an IDF component or a standalone
# project that forces ThorVG off and builds its tests, so neither branch is
# usable from here. The source list is a glob instead: every .cpp it carries
# except the ESP32 portability layer and its test and benchmark trees.
file(GLOB_RECURSE LVGL_CPP_SOURCES "${LVGL_CPP_DIR}/*.cpp")
list(FILTER LVGL_CPP_SOURCES EXCLUDE REGEX "/(tests|bench|examples|docs|scripts)/")
list(FILTER LVGL_CPP_SOURCES EXCLUDE REGEX "/(portable/)?esp32")

add_library(lvgl_cpp STATIC ${LVGL_CPP_SOURCES})
# SYSTEM: the binding's headers are included by our sources and carry warnings
# of their own (unused parameters in the gesture proxy), which are not ours to
# fix and would otherwise be the only noise in a -Wall -Wextra build.
target_include_directories(lvgl_cpp SYSTEM PUBLIC "${LVGL_CPP_DIR}" "${LVGL_CPP_DIR}/..")
target_link_libraries(lvgl_cpp PUBLIC lvgl)
target_compile_options(lvgl_cpp PRIVATE -Wno-cast-function-type -Wno-missing-field-initializers)

# ---------------------------------------------------------------------------
# The simulator itself
# ---------------------------------------------------------------------------

set(PORT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/MainSim.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PlatformSim.cpp
    ${CMAKE_CURRENT_LIST_DIR}/CanPortCanu.cpp
    ${CMAKE_CURRENT_LIST_DIR}/BlobStoreFile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MqttPortPaho.cpp
    ${CMAKE_CURRENT_LIST_DIR}/FirmwarePc.cpp
)

add_executable(espp4-sim
    ${KANARDIA_APP_SOURCES}
    ${PORT_SOURCES}
    ${KANARDIA_COMMON_SOURCES}
    ${KANARDIA_CANU_SOURCES}
    ${KANARDIA_LZO_SOURCES}
)

target_include_directories(espp4-sim PRIVATE
    ${KANARDIA_APP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_LIST_DIR})

target_include_directories(espp4-sim SYSTEM PRIVATE
    ${PAHO_MQTTPP_INCLUDE_DIR}
    ${PAHO_MQTTC_INCLUDE_DIR})

target_link_libraries(espp4-sim PRIVATE lvgl_cpp lvgl SDL2::SDL2
    ${PAHO_MQTTPP_LIBRARY} ${PAHO_MQTTC_LIBRARY})

target_compile_options(espp4-sim PRIVATE -Wall -Wextra)

# Section-level garbage collection, as the board's link has.
#
# Not an optimisation here: the hand-picked subset of Common in
# cmake/KanardiaSources.cmake leaves out leaves that nothing on either build
# actually calls -- avio::gnss::ReceiverNMEA::Parse() reached through a vtable
# that is never instantiated, file::AbstractFile::CheckVersion() behind a
# DateTime reader nothing reads. IDF links with --gc-sections and drops them.
# Without the same here the two builds would need two different source lists.
target_compile_options(espp4-sim PRIVATE -ffunction-sections -fdata-sections)
target_link_options(espp4-sim PRIVATE -Wl,--gc-sections)

# Common's own compile options, as on the board -- minus -DESP32, which selects
# the branch of Common/Defines.h that routes PRINTF to ESP_LOGI.
set_source_files_properties(${KANARDIA_COMMON_SOURCES} ${KANARDIA_CANU_SOURCES}
    DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    PROPERTIES COMPILE_OPTIONS "${KANARDIA_COMMON_FLAGS}")

# The fonts, from the same TTF through the same tool as the board's.
include("${KANARDIA_ROOT}/cmake/KanardiaFonts.cmake")
kanardia_add_fonts(espp4-sim)
