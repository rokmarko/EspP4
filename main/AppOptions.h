#pragma once

/**
 * @file AppOptions.h
 * @brief The option set this product keeps, and the way Settings walks it.
 *
 * `option::ModelBase` registers what every Kanardia product has -- units,
 * azimuth, aircraft -- and each product adds the rest. We add the last known
 * coordinate, because `avio::ModelBase` asks us to persist it and
 * `option::Key::LastKnownCoordinate` is where Common expects it to live.
 *
 * `option::Container` keeps its item list protected, which is the reason this
 * class exists at all: the three accessors below are what let `Settings` walk
 * the registered keys and their dirty flags without a friend declaration.
 */

#include "KanardiaCommon.h"

#include "Option/OptionLastKnownCoordinate.h"
#include "Option/OptionsModel.h"

#include <vector>

namespace app {

class Options : public option::ModelBase
{
public:
    Options();

    /** Every registered key, in registration order. */
    std::vector<option::Key> GetKeys() const;

    /** Whether that one option has changes not yet written to NVS. */
    bool IsDirty(option::Key eKey) const;

    /** Mark it saved. */
    void ClearDirty(option::Key eKey);

    option::LastKnownCoordinate m_lastKnown;
};

} // namespace app
