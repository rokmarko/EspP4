#pragma once

/**
 * @file KanardiaCommon.h
 * @brief Makes the Kanardia Public/Common headers usable without Qt.
 *
 * Common is normally compiled either for the LPC17xx firmware or against Qt.
 * A couple of leaf headers -- Parameter/ParamColors.h is the one we need --
 * reach for Qt's qRgb() even though nothing else about them is Qt-specific.
 * Supplying an identical constexpr qRgb() here lets those headers compile for
 * the ESP32-P4 unchanged, so the shared tree stays untouched.
 *
 * Include this *before* any header from the Parameter directory.
 */

#include "Gui/Rgb.h"

#include <concepts>
#include <type_traits>

namespace common {

/**
 * Mixed-type common::IsInside().
 *
 * Map/MapBase.h writes assert(common::IsInside(iLon, -180, 179)) with an
 * int32_t and two int literals. The MathEx template deduces one T from all
 * three arguments, and int32_t is `long` on this toolchain, so `long` against
 * `int` fails to deduce and the header does not compile. This overload widens
 * to the common type instead. The constraint keeps it out of the way whenever
 * the original same-type template applies, so nothing that already compiles
 * changes meaning.
 *
 * Upstream this wants to be spelled common::IsInside<int32_t>(...).
 */
template<typename T, typename U, typename V>
    requires (!std::same_as<T, U> || !std::same_as<T, V>)
constexpr bool IsInside(const T& v, const U& low, const V& high)
{
    using C = std::common_type_t<T, U, V>;
    return IsInside<C>(static_cast<C>(v), static_cast<C>(low), static_cast<C>(high));
}

} // namespace common

#if !defined(QT_CORE_LIB)

using QRgb = ::gui::ARGB;

/** Qt's qRgb(): opaque colour packed as 0xAARRGGBB. Same packing as gui::ARGB. */
constexpr QRgb qRgb(int r, int g, int b)
{
    return ::gui::GetRGB(r, g, b);
}

#endif /* !QT_CORE_LIB */
