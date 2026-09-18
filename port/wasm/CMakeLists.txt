# ---------------------------------------------------------------------------
# The Kaledi item renderer, as WebAssembly
#
#     . /home/rok/src/emsdk/emsdk_env.sh
#     emcmake cmake -S port/wasm -B build-wasm -G Ninja
#     cmake --build build-wasm
#     python3 -m http.server -d build-wasm      # then open demo/index.html
#
# The third build of these sources, after the board (main/CMakeLists.txt) and
# the desktop simulator (port/pc/CMakeLists.txt), and much the smallest: only
# src/Item and the ThorVG back end they draw through, with a headless LVGL
# behind them and an embind surface in front. No SDL, no CAN, no flight model,
# no settings store, no MQTT.
#
# The point of building it from the firmware's own sources rather than drawing
# the widgets a second time in JavaScript is that a layout editor's preview is
# worth nothing unless it is the thing the panel will draw. Every band, every
# pointer, every letter here comes out of the same code the aircraft flies
# with.
#
# Needs, beyond emscripten: lv_font_conv for the Kanardia fonts
# (npm install --prefix tools lv_font_conv) and the Public/Common tree, the
# same as the other two builds.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

project(kaledi_item LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "This project builds with emscripten. Configure it with:\n"
        "    . /home/rok/src/emsdk/emsdk_env.sh\n"
        "    emcmake cmake -S port/wasm -B build-wasm -G Ninja")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # ThorVG rasterises in software. An unoptimised build draws at a few frames
    # a second, which in an editor reads as the widget being slow rather than
    # the build being wrong.
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

get_filename_component(KANARDIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${KANARDIA_ROOT}/cmake/KanardiaSources.cmake")

# ---------------------------------------------------------------------------
# LVGL, and the C++ binding over it
#
# Identical to port/pc's, minus the SDL link: LVGL's own CMake never links SDL
# and never needs to here, because port/wasm/lv_conf.h has LV_USE_SDL 0.
# ---------------------------------------------------------------------------

set(MANAGED "${KANARDIA_ROOT}/managed_components")

set(LVGL_SRC_DIR "${MANAGED}/lvgl__lvgl" CACHE PATH "LVGL source tree")
set(LVGL_CPP_DIR "${MANAGED}/lvgl_cpp"   CACHE PATH "lvgl_cpp source tree")

if(NOT EXISTS "${LVGL_SRC_DIR}/lvgl.h" OR NOT EXISTS "${LVGL_CPP_DIR}/lvgl_cpp.h")
    # Same versions main/idf_component.yml pins, and the same ones port/pc
    # falls back to. Keep all three in step.
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

# lv_conf.h is ours -- port/pc's with LV_USE_SDL turned off; see its header.
# FORCE, because LVGL caches it and a stale entry from an earlier configure
# would silently win.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_SRC_DIR}" lvgl_build)

# lvgl_cpp's own CMakeLists.txt is either an IDF component or a standalone
# project that forces ThorVG off and builds its tests, so neither branch is
# usable from here -- the same glob port/pc uses.
file(GLOB_RECURSE LVGL_CPP_SOURCES "${LVGL_CPP_DIR}/*.cpp")
list(FILTER LVGL_CPP_SOURCES EXCLUDE REGEX "/(tests|bench|examples|docs|scripts)/")
list(FILTER LVGL_CPP_SOURCES EXCLUDE REGEX "/(portable/)?esp32")

add_library(lvgl_cpp STATIC ${LVGL_CPP_SOURCES})
target_include_directories(lvgl_cpp SYSTEM PUBLIC "${LVGL_CPP_DIR}" "${LVGL_CPP_DIR}/..")
target_link_libraries(lvgl_cpp PUBLIC lvgl)
target_compile_options(lvgl_cpp PRIVATE -Wno-cast-function-type -Wno-missing-field-initializers)

# ---------------------------------------------------------------------------
# The module
# ---------------------------------------------------------------------------

set(PORT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/PlatformWasm.cpp
    ${CMAKE_CURRENT_LIST_DIR}/LvglHeadless.cpp
    ${CMAKE_CURRENT_LIST_DIR}/KalediConfig.cpp
    ${CMAKE_CURRENT_LIST_DIR}/KalediRenderer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Bindings.cpp
)

add_executable(kaledi-item
    ${KANARDIA_ITEM_SOURCES}
    ${PORT_SOURCES}
    ${KANARDIA_COMMON_RENDER_SOURCES}
    ${KANARDIA_LZO_SOURCES}
)

target_include_directories(kaledi-item PRIVATE
    ${KANARDIA_APP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(kaledi-item PRIVATE lvgl_cpp lvgl)

target_compile_options(kaledi-item PRIVATE -Wall -Wextra)

# RapidJSON parses the editor's item and style objects, the same way
# src/MqttClient.cpp parses the cloud's messages. Without this it wants
# exceptions, which would pull emscripten's exception machinery into a module
# that has nothing to catch.
target_compile_definitions(kaledi-item PRIVATE RAPIDJSON_HAS_EXCEPTIONS=0)

# Section-level garbage collection, as both other builds have.
#
# Not an optimisation: the hand-picked subset of Common in
# cmake/KanardiaSources.cmake leaves out leaves that nothing actually calls but
# that a compiled translation unit still references. IDF links with
# --gc-sections and so does wasm-ld, by default; these two flags are what give
# it something to collect at.
target_compile_options(kaledi-item PRIVATE -ffunction-sections -fdata-sections)

target_link_options(kaledi-item PRIVATE
    --no-entry              # a library, not a program; there is no main()
    -lembind
    -sMODULARIZE=1          # the factory the demo page awaits
    -sEXPORT_ES6=1
    -sEXPORT_NAME=KalediItem
    -sALLOW_MEMORY_GROWTH=1 # a pixmap's size is the editor's to choose
    -sENVIRONMENT=web,worker,node   # node so that test/smoke.mjs can drive it
    -sFILESYSTEM=0          # nothing here opens a file
)

set_target_properties(kaledi-item PROPERTIES SUFFIX ".js")

# Common's own compile options, as on both other builds -- minus -DESP32, which
# selects the branch of Common/Defines.h that routes PRINTF to ESP_LOGI.
set_source_files_properties(${KANARDIA_COMMON_RENDER_SOURCES}
    DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    PROPERTIES COMPILE_OPTIONS "${KANARDIA_COMMON_FLAGS}")

# The fonts, from the same TTF through the same tool as the board's. The
# generated .c is plain C and compiles to wasm with everything else.
include("${KANARDIA_ROOT}/cmake/KanardiaFonts.cmake")
kanardia_add_fonts(kaledi-item)

# The demo page, beside the module it loads, and the package.json that lets
# Node import that module as the ES module it is.
configure_file("${CMAKE_CURRENT_LIST_DIR}/demo/index.html"
               "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/package.json"
               "${CMAKE_CURRENT_BINARY_DIR}/package.json" COPYONLY)
