/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// One parameter as a bare readout: title above, number and unit below.
//
// Nothing is drawn for the bands -- but they are still read, because the
// number is lettered in the colour of the band it is standing in. That is the
// whole of lasky::utils::Value, and it is what a parameter with no useful
// range of its own gets: a fuel total, a voltage, an outside air temperature.

#include "ItemBase.h"

namespace item {

class Value : public Base
{
public:
	Value(const Style& style, const Param& par, const Rect& rc) :
		m_style(style),
		m_pPar(&par),
		m_rc(rc)
	{}

	void DrawStatic(Painter& P) const override;
	void DrawDynamic(Painter& P) const override;

private:
	// The top of the title/value block, which is centred in the box. Both
	// halves have to agree on it.
	float BlockTop(Painter& P) const;

private:
	// What this item was built for and draws for the rest of its life.
	Style			 m_style;
	const Param* m_pPar;
	Rect			 m_rc;
};

} // namespace item
