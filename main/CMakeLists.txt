# ---------------------------------------------------------------------------
# Kanardia shared code
#
# The Public/Common tree lives outside this repository, in the SVN/git working
# copy that every other product builds from. Private/Horis/v1/CMakeLists.txt
# lists the files it needs one by one; we do the same here, because only a
# small corner of Common (parameter bands + the scale drawing) is wanted on the
# ESP32-P4, and Common as a whole would not compile for this target.
# ---------------------------------------------------------------------------

set(KANARDIA_BRANCH "$ENV{HOME}/Branch/v4_3" CACHE PATH
    "Root of the Kanardia v4_3 working copy (holds Public/ and Private/)")

set(COMMON  "${KANARDIA_BRANCH}/Public/Common")
# The microcontroller-side unit container lives in the private tree; the public
# one (CanUnitInfo/UnitInfoBaseContainer) is a QObject and cannot be used here.
set(PRIVATE_COMMON "${KANARDIA_BRANCH}/Private/Common")

if(NOT EXISTS "${COMMON}/Scale/ScaleStyle.h")
    message(FATAL_ERROR
        "Kanardia Public/Common not found under '${KANARDIA_BRANCH}'. "
        "Pass -DKANARDIA_BRANCH=/path/to/v4_3 to idf.py.")
endif()

# Parameter model: bands (coloured arcs), their colours and the unit plumbing
# ParamFunction.h drags in.
set(SRC_PARAM_FILES
    ${COMMON}/Parameter/ParamBands.cpp
    ${COMMON}/Parameter/ParamDefines.cpp
    ${COMMON}/Parameter/ParamFunction.cpp
    ${COMMON}/Parameter/ParamFormat.cpp
    ${COMMON}/Parameter/ParamUnitGroup.cpp
    # parameter::Parameter and the container that owns them, keyed by can::Id.
    ${COMMON}/Parameter/Param.cpp
    ${COMMON}/Parameter/ParamContainer.cpp
    ${COMMON}/Parameter/ParamFuelLevel.cpp
    # ParamStorage packs the whole container into one flatbuffer and LZO-
    # compresses it -- the parameter blob this product keeps in NVS.
    ${COMMON}/Parameter/ParamStorage.cpp
    ${COMMON}/CanAerospace/CanIdDetails.cpp
    ${COMMON}/Compress/CompressLZO.cpp
    ${COMMON}/CRC/CRC-32.cpp
)

# miniLZO is C, not C++, so it is kept out of SRC_COMMON_FILES: those get
# -include KanardiaCommon.h, which is a C++ header.
set(SRC_LZO_FILES
    ${COMMON}/LZO/minilzo.c
)

# Units: the key/conversion tables plus the UTF-8 formatter, which is what maps
# a unit onto the private-use glyph the product font draws for it.
set(SRC_UNIT_FILES
    ${COMMON}/Unit/UnitKeys.cpp
    ${COMMON}/Unit/AbstractUnitFormatter.cpp
    ${COMMON}/Unit/UnitFormatterUtf8.cpp
    ${COMMON}/Utf8Utils.cpp
    # avio::format is the layer above: it holds the one Formatter the whole
    # tree formats through, and turns values into the strings the product
    # shows. AbstractUnitFormatter.cpp already calls into it.
    ${COMMON}/Avio/Format/AvioFormat.cpp
)

# The scale itself. ScaleStyle.h / ScaleMarkings.h are header-only; ScaleUtils
# computes label/major/minor steps for a given range.
set(SRC_SCALE_FILES
    ${COMMON}/Scale/ScaleUtils.cpp
)

# The flight model: avio::ModelBase and everything it folds together --
# CAN network object data, GNSS, navigation, clock, sunrise/sunset.
set(SRC_MODEL_FILES
    ${COMMON}/Avio/Model/ModelBase.cpp
    ${COMMON}/Avio/Model/LastKnownCoordinate.cpp
)

set(SRC_GNSS_FILES
    ${COMMON}/Avio/GNSS/GNSS.cpp
    ${COMMON}/Avio/GNSS/GNSSBank.cpp
    ${COMMON}/Avio/GNSS/GNSSExtendedReceiverNMEA.cpp
    ${COMMON}/NMEA/NMEAParserExtended.cpp
)

set(SRC_NAVIGATION_FILES
    ${COMMON}/Avio/Navigation/NavigationCore.cpp
    ${COMMON}/Avio/Navigation/Navigation.cpp
    ${COMMON}/Avio/Navigation/NavigationWaypoint.cpp
    ${COMMON}/Avio/Navigation/NavigationRoute.cpp
)

# CANaerospace: the network object data store, the old-service stack (only the
# module-information half is switched on, see ApplicationDefines.h), the unit
# container and the abstract port the hardware driver implements.
set(SRC_CAN_FILES
    ${COMMON}/CanAerospace/CanNOD.cpp
    ${COMMON}/CanAerospace/CanOldServices.cpp
    ${COMMON}/CanAerospace/ModuleInfoService.cpp
    # The receive half of the buffer push: DDS_B takes the bytes, MCS_B's
    # apply-buffer command commits them. See main/ApplicationDefines.h.
    ${COMMON}/CanAerospace/DownloadService.cpp
    ${COMMON}/CanAerospace/ModuleConfigService.cpp
    ${COMMON}/CRC/CRC-16.cpp
    ${COMMON}/CanAerospace/StaticHelper.cpp
    ${COMMON}/CanAerospace/CanIdUtils.cpp
    ${COMMON}/CanPort/AbstractCanPort.cpp
    ${PRIVATE_COMMON}/Application/uCUnitInfoContainer.cpp
)

set(SRC_MAP_FILES
    ${COMMON}/Map/MapPrimitives.cpp
    ${COMMON}/Map/FlatEarth.cpp
)

set(SRC_OPTION_FILES
    ${COMMON}/Option/OptionBase.cpp
    ${COMMON}/Option/OptionsModel.cpp
    ${COMMON}/Option/OptionUnits.cpp
    ${COMMON}/Option/Serialize/SerializeUnits.cpp
    ${COMMON}/Option/Serialize/SerializeAzimuth.cpp
    ${COMMON}/Option/Serialize/SerializeAircraft.cpp
    # Registered by app::Options, not by option::ModelBase: avio::ModelBase
    # asks us to persist the last known position and this is Common's own slot
    # for it.
    ${COMMON}/Option/Serialize/SerializeLastKnownCoordinate.cpp
    ${COMMON}/BLOB/BLOB.cpp
    ${COMMON}/BLOB/BLOBPackUnpack.cpp
    ${COMMON}/Compress/CompressZeros.cpp
)

# Leaf utilities the model reaches for: time, sun position, hysteresis.
set(SRC_UTIL_FILES
    ${COMMON}/AboveBelow.cpp
    ${COMMON}/Clock.cpp
    ${COMMON}/SunriseSunset.cpp
    ${COMMON}/SystemTime.cpp
    ${COMMON}/JulianDay.cpp
    ${COMMON}/DateTime.cpp
    ${COMMON}/DateTimeHelper.cpp
    ${COMMON}/Format.cpp
)

set(SRC_COMMON_FILES
    ${SRC_PARAM_FILES}
    ${SRC_UNIT_FILES}
    ${SRC_SCALE_FILES}
    ${SRC_MODEL_FILES}
    ${SRC_GNSS_FILES}
    ${SRC_NAVIGATION_FILES}
    ${SRC_CAN_FILES}
    ${SRC_MAP_FILES}
    ${SRC_OPTION_FILES}
    ${SRC_UTIL_FILES}
)

idf_component_register(
    SRCS "Main.cpp"
         "AppModel.cpp"
         "AppOptions.cpp"
         "AppParameters.cpp"
         "CanPortEsp.cpp"
         "CanProcessor.cpp"
         "StorageOptions.cpp"
         "VectorScene.cpp"
         "ScaleDrawTvg.cpp"
         "SerialConsole.cpp"
         ${SRC_COMMON_FILES}
         ${SRC_LZO_FILES}
    INCLUDE_DIRS "." "${COMMON}" "${COMMON}/ThirdParty" "${PRIVATE_COMMON}"
    # StorageOptions.cpp keeps the option and parameter blobs in the
    # `settings` NVS partition.
    PRIV_REQUIRES nvs_flash)

# Common comes from a very different toolchain. Two adjustments:
#
#  - its style must not turn into errors here, and its printf formats assume a
#    toolchain where uint32_t is `unsigned int` rather than `long`;
#  - Map/MapBase.h does not compile without the mixed-type common::IsInside()
#    overload in KanardiaCommon.h, and a Common source will never include a
#    project header on its own, so prepend it on the command line.
#
# This has to sit after idf_component_register: IDF re-runs the file in script
# mode to collect requirements, and set_source_files_properties is not
# scriptable there.
# NO_LZO_COMPRESSION is Common's own switch for targets that do not carry
# miniLZO. Only BLOBPackUnpack.cpp looks at it, and nothing here produces
# LZO-compressed BLOBs.
set(COMMON_FLAGS
    -DNO_LZO_COMPRESSION
    # Common/Defines.h routes PRINTF to ESP_LOGI behind `defined(ESP32)`, and
    # IDF does not define that for the P4. It is the only ESP32 test in the
    # whole tree, so defining it just picks the logging branch Common intends.
    -DESP32
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-format
    -include "${CMAKE_CURRENT_LIST_DIR}/KanardiaCommon.h")

set_source_files_properties(${SRC_COMMON_FILES} DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    PROPERTIES COMPILE_OPTIONS "${COMMON_FLAGS}")

# These two throw on malformed input -- `throw std::runtime_error("Unsupported
# BLOB compression.")` and friends -- which will not compile under IDF's global
# -fno-exceptions. The throws sit on branches this firmware never reaches (the
# option serializers only ever produce uncompressed BLOBs), but they still have
# to compile, so these files get exceptions back. If one ever did throw,
# unwinding would hit a -fno-exceptions frame and terminate.
set_source_files_properties(
    ${COMMON}/BLOB/BLOBPackUnpack.cpp
    ${COMMON}/Compress/CompressZeros.cpp
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


# ---------------------------------------------------------------------------
# Fonts
#
# Kanardia.ttf is the product font: Roboto with Kanardia's own unit glyphs
# merged in, and those live in the private-use area from 0xE000. It is turned
# into C at build time by lv_font_conv -- LVGL's own font pipeline, the same
# tool scripts/built_in_font/ uses to generate the Montserrat fonts -- so no
# generated font source is ever checked in and a size change is a one-line
# edit here.
#
# lv_font_conv is a Node tool and is not part of ESP-IDF. Install it once:
#
#     npm install --prefix tools lv_font_conv
#
# or globally, or point -DLV_FONT_CONV=/path/to/lv_font_conv at an existing one.
# ---------------------------------------------------------------------------

set(KANARDIA_FONT_TTF "${KANARDIA_BRANCH}/Public/Font/Kanardia.ttf"
    CACHE FILEPATH "TTF the LVGL fonts are generated from")

set(KANARDIA_FONT_SIZES "14;16;20;28"
    CACHE STRING "Pixel sizes to generate, smallest first")

# ASCII, degree sign, bullet -- LVGL's own default range -- plus the unit
# glyphs. The font carries 0xE000..0xE118 in the private-use area; anything in
# the range with no glyph behind it is simply skipped by lv_font_conv.
set(KANARDIA_FONT_RANGES "0x20-0x7F,0xB0,0x2022,0xE000-0xE118"
    CACHE STRING "Codepoints to include")

set(KANARDIA_FONT_BPP "4"
    CACHE STRING "Anti-aliasing depth; 4 is what LVGL's built-in fonts use")

find_program(LV_FONT_CONV
    NAMES lv_font_conv
    HINTS "${CMAKE_CURRENT_LIST_DIR}/../tools/node_modules/.bin"
    DOC "LVGL font converter (npm install --prefix tools lv_font_conv)")

if(NOT LV_FONT_CONV)
    message(FATAL_ERROR
        "lv_font_conv not found -- the Kanardia fonts are generated at build "
        "time and cannot be built without it.\n"
        "  npm install --prefix ${CMAKE_CURRENT_LIST_DIR}/../tools lv_font_conv\n"
        "or pass -DLV_FONT_CONV=/path/to/lv_font_conv.")
endif()

if(NOT EXISTS "${KANARDIA_FONT_TTF}")
    message(FATAL_ERROR "Font not found: ${KANARDIA_FONT_TTF}")
endif()

set(FONT_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/fonts")
file(MAKE_DIRECTORY "${FONT_OUT_DIR}")

set(KANARDIA_FONT_SRCS "")
set(KANARDIA_FONT_DECLS "")
set(KANARDIA_FONT_XLIST "")

foreach(size IN LISTS KANARDIA_FONT_SIZES)
    # lv_font_conv derives both the C symbol and its include guard from the
    # output file name, so the file name is the font name.
    set(font_name "lv_font_kanardia_${size}")
    set(font_src  "${FONT_OUT_DIR}/${font_name}.c")

    add_custom_command(
        OUTPUT "${font_src}"
        COMMAND "${LV_FONT_CONV}"
                --font "${KANARDIA_FONT_TTF}"
                -r "${KANARDIA_FONT_RANGES}"
                --size ${size}
                --bpp ${KANARDIA_FONT_BPP}
                --no-compress --no-prefilter
                --force-fast-kern-format
                --format lvgl
                -o "${font_src}"
        DEPENDS "${KANARDIA_FONT_TTF}"
        COMMENT "lv_font_conv: Kanardia ${size} px"
        VERBATIM)

    list(APPEND KANARDIA_FONT_SRCS "${font_src}")
    string(APPEND KANARDIA_FONT_DECLS "LV_FONT_DECLARE(${font_name})\n")
    string(APPEND KANARDIA_FONT_XLIST "    X(${size}, ${font_name}) \\\n")
endforeach()

# The header is generated alongside the fonts so the two can never drift: edit
# KANARDIA_FONT_SIZES and the declarations follow.
#
# It is written once at configure time and then *copied* into place by a build
# rule, rather than written straight to its final path. Ninja can only rebuild
# what it has a rule for, and without the copy step a deleted header stays
# deleted until the next reconfigure.
set(FONT_HEADER_IN  "${CMAKE_CURRENT_BINARY_DIR}/font_template/KanardiaFont.h")
set(FONT_HEADER_OUT "${FONT_OUT_DIR}/KanardiaFont.h")

file(WRITE "${FONT_HEADER_IN}" "#pragma once

/*
 * Generated by main/CMakeLists.txt from ${KANARDIA_FONT_TTF} -- do not edit.
 * Ranges: ${KANARDIA_FONT_RANGES}
 */

#include \"lvgl.h\"

${KANARDIA_FONT_DECLS}
/** Every generated size, smallest first: X(px, symbol). */
#define KANARDIA_FONT_LIST(X) \\
${KANARDIA_FONT_XLIST}
")

add_custom_command(
    OUTPUT "${FONT_HEADER_OUT}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FONT_HEADER_IN}" "${FONT_HEADER_OUT}"
    DEPENDS "${FONT_HEADER_IN}"
    COMMENT "Generating KanardiaFont.h"
    VERBATIM)

add_custom_target(kanardia_fonts DEPENDS ${KANARDIA_FONT_SRCS} "${FONT_HEADER_OUT}")

# The generated sources say #include \"lvgl/lvgl.h\" unless told otherwise, and
# under ESP-IDF the header is plain \"lvgl.h\".
set_source_files_properties(${KANARDIA_FONT_SRCS}
    DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    PROPERTIES GENERATED TRUE COMPILE_DEFINITIONS "LV_LVGL_H_INCLUDE_SIMPLE")

target_sources(${COMPONENT_LIB} PRIVATE ${KANARDIA_FONT_SRCS})
target_include_directories(${COMPONENT_LIB} PUBLIC "${FONT_OUT_DIR}")
add_dependencies(${COMPONENT_LIB} kanardia_fonts)
