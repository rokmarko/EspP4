# ---------------------------------------------------------------------------
# The ESP-IDF component
#
# Everything this builds lives outside main/: src/ is the product, port/esp/ is
# what only the board can provide, and cmake/ holds the lists both builds
# share. This file is the ESP-IDF end of that -- IDF insists on a component
# directory, and this is it.
#
# The desktop simulator is port/pc/CMakeLists.txt and compiles the same src/
# against port/pc/.
# ---------------------------------------------------------------------------

get_filename_component(KANARDIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${KANARDIA_ROOT}/cmake/KanardiaSources.cmake")

# What only the board can answer: the panel, the TWAI controller, NVS and the
# OTA slots.
set(PORT_SOURCES
    ${KANARDIA_ROOT}/port/esp/MainEsp.cpp
    ${KANARDIA_ROOT}/port/esp/PlatformEsp.cpp
    ${KANARDIA_ROOT}/port/esp/CanPortEsp.cpp
    ${KANARDIA_ROOT}/port/esp/BlobStoreNvs.cpp
    ${KANARDIA_ROOT}/port/esp/FirmwareEsp.cpp
)

idf_component_register(
    SRCS ${KANARDIA_APP_SOURCES}
         ${PORT_SOURCES}
         ${KANARDIA_COMMON_SOURCES}
         ${KANARDIA_LZO_SOURCES}
    INCLUDE_DIRS ${KANARDIA_APP_INCLUDE_DIRS} "${KANARDIA_ROOT}/port/esp"
    # BlobStoreNvs.cpp keeps the option and parameter blobs in the `settings`
    # NVS partition.
    # app_update is the OTA API FirmwareEsp.cpp writes through.
    PRIV_REQUIRES nvs_flash pthread app_update)

# Common's own compile options.
#
# This has to sit after idf_component_register: IDF re-runs the file in script
# mode to collect requirements, and set_source_files_properties is not
# scriptable there.
#
# -DESP32 is the board's alone: Common/Defines.h routes PRINTF to ESP_LOGI
# behind `defined(ESP32)`, and IDF does not define that for the P4. It is the
# only ESP32 test in the whole tree, so defining it just picks the logging
# branch Common intends.
set(COMMON_FLAGS ${KANARDIA_COMMON_FLAGS} -DESP32)

set_source_files_properties(${KANARDIA_COMMON_SOURCES} DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    PROPERTIES COMPILE_OPTIONS "${COMMON_FLAGS}")

# The two that throw. IDF compiles C++ with -fno-exceptions globally, so these
# get them back; if one ever did throw, unwinding would hit a -fno-exceptions
# frame and terminate.
set_source_files_properties(
    ${KANARDIA_COMMON_EXC_SOURCES}
    DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    PROPERTIES COMPILE_OPTIONS "${COMMON_FLAGS};-fexceptions")

# LVGL vendors ThorVG (C++) under src/libs/thorvg, and its Lottie parser drags in
# RapidJSON. ESP-IDF compiles C++ without exceptions, so tell RapidJSON up front.
idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    if(TARGET ${lvgl_lib})
        target_compile_definitions(${lvgl_lib} PRIVATE RAPIDJSON_HAS_EXCEPTIONS=0)
        target_compile_options(${lvgl_lib} PRIVATE -Wno-format -Wno-error=cpp -Wno-error=attributes)
    endif()
endif()

# The Kanardia fonts, generated from the TTF by lv_font_conv -- the same
# function the simulator calls, so both builds draw with the same glyphs.
include("${KANARDIA_ROOT}/cmake/KanardiaFonts.cmake")
kanardia_add_fonts(${COMPONENT_LIB})
