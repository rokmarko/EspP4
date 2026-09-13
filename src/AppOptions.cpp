/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2019 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *   Writen by:                                                            *
 *      Rok Markovic [rok.markovic@kanardia.eu]                            *
 *                                                                         *
 *   Status: Open Source                                                   *
 *                                                                         *
 *   License: GPL - GNU General Public License                             *
 *                                                                         *
 ***************************************************************************/

#include "AppOptions.h"

#include "Option/Serialize/SerializeLastKnownCoordinate.h"

namespace app {

Options::Options()
{
	// option::ModelBase's constructor has already registered units, azimuth
	// and aircraft; this is the one we add.
	Register(option::Key::LastKnownCoordinate, m_lastKnown, std::make_unique<option::serialize::LastKnownCoordinate>());
}

// --------------------------------------------------------------------------

std::vector<option::Key> Options::GetKeys() const
{
	std::vector<option::Key> vKeys;
	vKeys.reserve(m_vItems.size());
	for(const auto& item : m_vItems)
		vKeys.push_back(item.eKey);
	return vKeys;
}

// --------------------------------------------------------------------------

bool Options::IsDirty(option::Key eKey) const
{
	const Item* pItem = Find(eKey);
	return pItem != nullptr && pItem->base.IsDirty();
}

// --------------------------------------------------------------------------

void Options::ClearDirty(option::Key eKey)
{
	if(Item* pItem = Find(eKey))
		pItem->base.ClearDirty();
}

} // namespace app
