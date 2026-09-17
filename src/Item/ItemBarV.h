/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// One parameter as an upright bar: title on top, bands up the middle, readout
// underneath, pointer on the left flank.
//
// This is lasky::utils::VerticalBar's layout. It is what a column of engine
// temperatures and pressures is drawn with, because a bar packs into a narrow
// box that an arc cannot use.

#include "ItemBase.h"

namespace item {

class BarV : public Base
{
public:
	BarV(const Style& style, const Param& par, const Rect& rc) :
		m_style(style),
		m_pPar(&par),
		m_rc(rc)
	{}

	void DrawStatic(Painter& P) const override;
	void DrawDynamic(Painter& P) const override;

private:
	// The bar this item fills its box with, running bottom to top so the value
	// grows upwards. Both halves work it out the same way.
	Bar2D MakeBar(Painter& P) const;

private:
	// What this item was built for and draws for the rest of its life.
	Style			 m_style;
	const Param* m_pPar;
	Rect			 m_rc;
};

} // namespace item
