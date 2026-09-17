/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// One parameter as a bar lying on its side: title in a column on the left,
// readout in a column on the right, bands between the two, pointer above.
//
// This is lasky::utils::HorizontalBar's layout, and the two columns are fixed
// fractions of the box for the reason Nesis fixes them too: the readout is
// redrawn every frame and its width changes with the number, so anything
// measured off it would leave the bar's end shivering.

#include "ItemBase.h"

namespace item {

class BarH : public Base
{
public:
	BarH(const Style& style, const Param& par, const Rect& rc) :
		m_style(style),
		m_pPar(&par),
		m_rc(rc)
	{}

	void DrawStatic(Painter& P) const override;
	void DrawDynamic(Painter& P) const override;

private:
	// The bar this item fills its box with, running left to right. Both halves
	// work it out the same way.
	Bar2D MakeBar() const;

private:
	// What this item was built for and draws for the rest of its life.
	Style			 m_style;
	const Param* m_pPar;
	Rect			 m_rc;
};

} // namespace item
