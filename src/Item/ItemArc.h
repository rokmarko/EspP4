/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// One parameter as a 270-degree band arc, with the gap at the bottom.
//
// Title across the top, the coloured bands on the arc, and the readout sitting
// in the gap the arc leaves under itself -- the shape lasky::utils::Arc draws,
// and the reason its bounding box is 2 by 1.707: a full radius above the
// centre and sin 45 below it, which is as far down as the two ends reach.
//
// There are no dashes and no labels. That is what separates an item from
// scale::Scale: an item is read at a glance off the band a pointer stands in,
// and a panel of a dozen of them has no room for lettering each one.

#include "ItemBase.h"

namespace item {

class Arc : public Base
{
public:
	Arc(const Style& style, const Param& par, const Rect& rc) :
		m_style(style),
		m_pPar(&par),
		m_rc(rc)
	{}

	void DrawStatic(Painter& P) const override;
	void DrawDynamic(Painter& P) const override;

private:
	// The arc this item fills its box with. Both halves work it out the same
	// way, which is what keeps the pointer on the band the background drew. No
	// other kind has a centre to place, so this is the arc's own business.
	Arc2D MakeArc(Painter& P) const;

private:
	// What this item was built for and draws for the rest of its life.
	Style			 m_style;
	const Param* m_pPar;
	Rect			 m_rc;
};

} // namespace item
