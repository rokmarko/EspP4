# ---------------------------------------------------------------------------
# What both builds compile
#
# The application is built twice: for the ESP32-P4 panel (main/CMakeLists.txt,
# an ESP-IDF component) and for a desktop simulator (port/pc/CMakeLists.txt, a
# plain CMake project). Everything they have in common is here, so the two
# never drift into compiling different code and calling it the same product.
#
# Including this file requires KANARDIA_BRANCH to be set and defines:
#
#   KANARDIA_ROOT             this repository
#   KANARDIA_COMMON           the shared Public/Common tree
#   KANARDIA_PRIVATE_COMMON   the private tree, for uCUnitInfoContainer
#   KANARDIA_APP_SOURCES      src/*.cpp -- the portable half of the product
#   KANARDIA_APP_INCLUDE_DIRS what those need on the include path
#   KANARDIA_COMMON_SOURCES   the hand-picked subset of Common, C++
#   KANARDIA_LZO_SOURCES      miniLZO, which is C
#   KANARDIA_CANU_SOURCES     can::CanuCan and its CRC, desktop only
#   KANARDIA_COMMON_FLAGS     what Common has to be compiled with here
#   KANARDIA_COMMON_EXC_SOURCES  the two Common files that need exceptions
#
# The Public/Common tree lives outside this repository, in the working copy
# every other product builds from. Private/Horis/v1/CMakeLists.txt lists the
# files it needs one by one; we do the same, because only a corner of Common is
# wanted here and Common as a whole would not compile for the board.
# ---------------------------------------------------------------------------

get_filename_component(KANARDIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(KANARDIA_BRANCH "$ENV{HOME}/Branch/v4_3" CACHE PATH
    "Root of the Kanardia v4_3 working copy (holds Public/ and Private/)")

set(KANARDIA_COMMON "${KANARDIA_BRANCH}/Public/Common")
# The microcontroller-side unit container lives in the private tree; the public
# one (CanUnitInfo/UnitInfoBaseContainer) is a QObject and cannot be used here.
set(KANARDIA_PRIVATE_COMMON "${KANARDIA_BRANCH}/Private/Common")

if(NOT EXISTS "${KANARDIA_COMMON}/Scale/ScaleStyle.h")
    message(FATAL_ERROR
        "Kanardia Public/Common not found under '${KANARDIA_BRANCH}'. "
        "Pass -DKANARDIA_BRANCH=/path/to/v4_3.")
endif()

# ---------------------------------------------------------------------------
# The product
# ---------------------------------------------------------------------------

set(KANARDIA_APP_SOURCES
    ${KANARDIA_ROOT}/src/App.cpp
    ${KANARDIA_ROOT}/src/AppModel.cpp
    ${KANARDIA_ROOT}/src/AppOptions.cpp
    ${KANARDIA_ROOT}/src/AppParameters.cpp
    ${KANARDIA_ROOT}/src/CanProcessor.cpp
    ${KANARDIA_ROOT}/src/PainterTvg.cpp
    ${KANARDIA_ROOT}/src/ScaleDraw.cpp
    ${KANARDIA_ROOT}/src/SerialConsole.cpp
    ${KANARDIA_ROOT}/src/StorageOptions.cpp
    ${KANARDIA_ROOT}/src/VectorScene.cpp
)

set(KANARDIA_APP_INCLUDE_DIRS
    ${KANARDIA_ROOT}/src
    ${KANARDIA_COMMON}
    ${KANARDIA_COMMON}/ThirdParty
    ${KANARDIA_PRIVATE_COMMON}
)

# ---------------------------------------------------------------------------
# Kanardia Common
# ---------------------------------------------------------------------------

# Parameter model: bands (coloured arcs), their colours and the unit plumbing
# ParamFunction.h drags in.
set(SRC_PARAM_FILES
    ${KANARDIA_COMMON}/Parameter/ParamBands.cpp
    ${KANARDIA_COMMON}/Parameter/ParamDefines.cpp
    ${KANARDIA_COMMON}/Parameter/ParamFunction.cpp
    ${KANARDIA_COMMON}/Parameter/ParamFormat.cpp
    ${KANARDIA_COMMON}/Parameter/ParamUnitGroup.cpp
    # parameter::Parameter and the container that owns them, keyed by can::Id.
    ${KANARDIA_COMMON}/Parameter/Param.cpp
    ${KANARDIA_COMMON}/Parameter/ParamContainer.cpp
    ${KANARDIA_COMMON}/Parameter/ParamFuelLevel.cpp
    # ParamStorage packs the whole container into one flatbuffer and LZO-
    # compresses it -- the parameter blob this product keeps in its store.
    ${KANARDIA_COMMON}/Parameter/ParamStorage.cpp
    ${KANARDIA_COMMON}/CanAerospace/CanIdDetails.cpp
    ${KANARDIA_COMMON}/Compress/CompressLZO.cpp
    ${KANARDIA_COMMON}/CRC/CRC-32.cpp
)

# miniLZO is C, not C++, so it is kept out of KANARDIA_COMMON_SOURCES: those
# get -include KanardiaCommon.h, which is a C++ header.
set(KANARDIA_LZO_SOURCES
    ${KANARDIA_COMMON}/LZO/minilzo.c
)

# Units: the key/conversion tables plus the UTF-8 formatter, which is what maps
# a unit onto the private-use glyph the product font draws for it.
set(SRC_UNIT_FILES
    ${KANARDIA_COMMON}/Unit/UnitKeys.cpp
    ${KANARDIA_COMMON}/Unit/AbstractUnitFormatter.cpp
    ${KANARDIA_COMMON}/Unit/UnitFormatterUtf8.cpp
    ${KANARDIA_COMMON}/Utf8Utils.cpp
    # avio::format is the layer above: it holds the one Formatter the whole
    # tree formats through, and turns values into the strings the product
    # shows. AbstractUnitFormatter.cpp already calls into it.
    ${KANARDIA_COMMON}/Avio/Format/AvioFormat.cpp
)

# The scale itself. ScaleStyle.h / ScaleMarkings.h are header-only; ScaleUtils
# computes label/major/minor steps for a given range.
set(SRC_SCALE_FILES
    ${KANARDIA_COMMON}/Scale/ScaleUtils.cpp
)

# The flight model: avio::ModelBase and everything it folds together --
# CAN network object data, GNSS, navigation, clock, sunrise/sunset.
set(SRC_MODEL_FILES
    ${KANARDIA_COMMON}/Avio/Model/ModelBase.cpp
    ${KANARDIA_COMMON}/Avio/Model/LastKnownCoordinate.cpp
)

set(SRC_GNSS_FILES
    ${KANARDIA_COMMON}/Avio/GNSS/GNSS.cpp
    ${KANARDIA_COMMON}/Avio/GNSS/GNSSBank.cpp
    ${KANARDIA_COMMON}/Avio/GNSS/GNSSExtendedReceiverNMEA.cpp
    ${KANARDIA_COMMON}/NMEA/NMEAParserExtended.cpp
)

set(SRC_NAVIGATION_FILES
    ${KANARDIA_COMMON}/Avio/Navigation/NavigationCore.cpp
    ${KANARDIA_COMMON}/Avio/Navigation/Navigation.cpp
    ${KANARDIA_COMMON}/Avio/Navigation/NavigationWaypoint.cpp
    ${KANARDIA_COMMON}/Avio/Navigation/NavigationRoute.cpp
)

# CANaerospace: the network object data store, the old-service stack (only the
# module-information half is switched on, see ApplicationDefines.h), the unit
# container and the abstract port the hardware driver implements.
set(SRC_CAN_FILES
    ${KANARDIA_COMMON}/CanAerospace/CanNOD.cpp
    ${KANARDIA_COMMON}/CanAerospace/SOLAutoId.cpp
    ${KANARDIA_COMMON}/CanAerospace/CanOldServices.cpp
    ${KANARDIA_COMMON}/CanAerospace/ModuleInfoService.cpp
    ${KANARDIA_COMMON}/CanAerospace/DownloadService.cpp
    ${KANARDIA_COMMON}/CanAerospace/ModuleConfigService.cpp
    ${KANARDIA_COMMON}/CanAerospace/ApplicationProgrammingService.cpp
    ${KANARDIA_COMMON}/CRC/CRC-16.cpp
    ${KANARDIA_COMMON}/CanAerospace/StaticHelper.cpp
    ${KANARDIA_COMMON}/CanAerospace/CanIdUtils.cpp
    ${KANARDIA_COMMON}/CanPort/AbstractCanPort.cpp
    ${KANARDIA_PRIVATE_COMMON}/Application/uCUnitInfoContainer.cpp
)

# can::CanuCan, the desktop port: the framed serial protocol the Kanardia CANU
# v2 adapter speaks. Only the simulator builds it -- the board has a TWAI
# controller of its own -- so it is a list on its own, and CRC-8 comes with it
# because nothing else in this product uses that one.
set(KANARDIA_CANU_SOURCES
    ${KANARDIA_COMMON}/CanPort/CanuCan.cpp
    ${KANARDIA_COMMON}/CRC/CRC-8.cpp
)

set(SRC_MAP_FILES
    ${KANARDIA_COMMON}/Map/MapPrimitives.cpp
    ${KANARDIA_COMMON}/Map/FlatEarth.cpp
)

set(SRC_OPTION_FILES
    ${KANARDIA_COMMON}/Option/OptionBase.cpp
    ${KANARDIA_COMMON}/Option/OptionsModel.cpp
    ${KANARDIA_COMMON}/Option/OptionUnits.cpp
    ${KANARDIA_COMMON}/Option/Serialize/SerializeUnits.cpp
    ${KANARDIA_COMMON}/Option/Serialize/SerializeAzimuth.cpp
    ${KANARDIA_COMMON}/Option/Serialize/SerializeAircraft.cpp
    # Registered by app::Options, not by option::ModelBase: avio::ModelBase
    # asks us to persist the last known position and this is Common's own slot
    # for it.
    ${KANARDIA_COMMON}/Option/Serialize/SerializeLastKnownCoordinate.cpp
    ${KANARDIA_COMMON}/BLOB/BLOB.cpp
    ${KANARDIA_COMMON}/BLOB/BLOBPackUnpack.cpp
    ${KANARDIA_COMMON}/Compress/CompressZeros.cpp
)

# Leaf utilities the model reaches for: time, sun position, hysteresis.
set(SRC_UTIL_FILES
    ${KANARDIA_COMMON}/AboveBelow.cpp
    ${KANARDIA_COMMON}/Clock.cpp
    ${KANARDIA_COMMON}/SunriseSunset.cpp
    ${KANARDIA_COMMON}/SystemTime.cpp
    ${KANARDIA_COMMON}/JulianDay.cpp
    ${KANARDIA_COMMON}/DateTime.cpp
    ${KANARDIA_COMMON}/DateTimeHelper.cpp
    ${KANARDIA_COMMON}/Format.cpp
)

set(KANARDIA_COMMON_SOURCES
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

# These two throw on malformed input -- `throw std::runtime_error("Unsupported
# BLOB compression.")` and friends. The board compiles C++ without exceptions,
# so they are given them back there; the desktop has them anyway. The throws sit
# on branches this product never reaches (the option serializers only ever
# produce uncompressed BLOBs), but they still have to compile.
set(KANARDIA_COMMON_EXC_SOURCES
    ${KANARDIA_COMMON}/BLOB/BLOBPackUnpack.cpp
    ${KANARDIA_COMMON}/Compress/CompressZeros.cpp
)

# Common comes from a very different toolchain. Two adjustments:
#
#  - its style must not turn into errors here, and its printf formats assume a
#    toolchain where uint32_t is `unsigned int` rather than `long`;
#  - Map/MapBase.h does not compile without the mixed-type common::IsInside()
#    overload in KanardiaCommon.h, and a Common source will never include a
#    project header on its own, so prepend it on the command line.
#
# NO_LZO_COMPRESSION is Common's own switch for targets that do not carry
# miniLZO. Only BLOBPackUnpack.cpp looks at it, and nothing here produces
# LZO-compressed BLOBs.
set(KANARDIA_COMMON_FLAGS
    -DNO_LZO_COMPRESSION
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-format
    -include "${KANARDIA_ROOT}/src/KanardiaCommon.h")
