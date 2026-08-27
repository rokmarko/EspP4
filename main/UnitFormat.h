#pragma once

/**
 * @file UnitFormat.h
 * @brief Values printed the way the rest of the Kanardia code prints them.
 *
 * `unit::FormatterUtf8` turns a `unit::Key` into the glyph the product font
 * draws for it, and those glyphs live in the private-use area the build exports
 * out of Kanardia.ttf -- so "km/h" is one glyph rather than four characters.
 * `unit::Convert()` does the unit arithmetic, which means a value can be held
 * in whatever unit the CAN bus carries and shown in whatever the pilot picked.
 *
 * Note on Common/Unit/Value.h: it opens with `#error "Obsolete"`, so the class
 * that used to pair a value with its unit cannot be used. That pairing is now
 * a plain (float, unit::Key), which is what these helpers take.
 */

#include "KanardiaCommon.h"

#include "Unit/UnitFormatterUtf8.h"
#include "Unit/UnitKeys.h"

#include <string>

namespace app {

/** The one formatter. unit::Formatter is non-copyable, hence the reference. */
const unit::Formatter &GetUnitFormatter();

/**
 * The unit's own glyph, or its plain ASCII signature where the font has no
 * glyph for it -- rpm and percent, among others, never got one.
 *
 * @param bShort  prefer the narrow variant of the glyph where one exists.
 */
std::string UnitText(unit::Key eKey, bool bShort = false);

/** Value and unit, e.g. "244 <km/h glyph>". */
std::string FormatValue(float fValue, unit::Key eKey,
                        int iDecimals = 0, bool bShort = false);

/** As above, but converting from @p eFrom into @p eTo first. */
std::string FormatValue(float fValue, unit::Key eFrom, unit::Key eTo,
                        int iDecimals = 0, bool bShort = false);

} // namespace app
