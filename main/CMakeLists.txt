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

set(COMMON "${KANARDIA_BRANCH}/Public/Common")

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
    ${COMMON}/Parameter/ParamUnitGroup.cpp
)

set(SRC_UNIT_FILES
    ${COMMON}/Unit/UnitKeys.cpp
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

set(SRC_CAN_FILES
    ${COMMON}/CanAerospace/CanNOD.cpp
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
         "VectorScene.cpp"
         "ScaleDrawTvg.cpp"
         "SerialConsole.cpp"
         ${SRC_COMMON_FILES}
    INCLUDE_DIRS "." "${COMMON}" "${COMMON}/ThirdParty")

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
