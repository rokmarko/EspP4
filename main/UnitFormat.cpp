/**
 * @file UnitFormat.cpp
 */

#include "UnitFormat.h"

#include <cstdio>

namespace app {

namespace {

/* Function-local so it is built on first use, not during static init. */
const unit::FormatterUtf8 &Utf8Formatter()
{
    static const unit::FormatterUtf8 formatter;
    return formatter;
}

} // namespace

// --------------------------------------------------------------------------

const unit::Formatter &GetUnitFormatter()
{
    return Utf8Formatter();
}

// --------------------------------------------------------------------------

std::string UnitText(unit::Key eKey, bool bShort)
{
    const auto &fmt = Utf8Formatter();

    /* Format() answers with an empty string when the font carries no glyph for
     * the key, and the short variant is missing far more often than the
     * standard one. Fall back the whole way down to the ASCII signature. */
    std::string ss = bShort ? fmt.Format(eKey, unit::FormatType::ShortGlyph)
                            : std::string();
    if (ss.empty()) ss = fmt.Format(eKey, unit::FormatType::Glyph);
    if (ss.empty()) ss = fmt.Format(eKey, unit::FormatType::Signature);
    return ss;
}

// --------------------------------------------------------------------------

std::string FormatValue(float fValue, unit::Key eKey, int iDecimals, bool bShort)
{
    char szNumber[32];
    std::snprintf(szNumber, sizeof(szNumber), "%.*f", iDecimals,
                  static_cast<double>(fValue));

    std::string ss(szNumber);
    const std::string ssUnit = UnitText(eKey, bShort);
    if (ssUnit.empty() == false) {
        ss += ' ';
        ss += ssUnit;
    }
    return ss;
}

// --------------------------------------------------------------------------

std::string FormatValue(float fValue, unit::Key eFrom, unit::Key eTo,
                        int iDecimals, bool bShort)
{
    return FormatValue(unit::Convert(fValue, eFrom, eTo), eTo, iDecimals, bShort);
}

} // namespace app
