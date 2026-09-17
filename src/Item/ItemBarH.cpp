/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "ItemBarH.h"

#include "Parameter/Param.h"

#include <algorithm>

namespace item {

namespace {

	// The two lettered columns, as fractions of the item's width. The bar
	// gets whatever is left between them.
	constexpr float NAME_COL  = 0.24f;
	constexpr float VALUE_COL = 0.38f;

} // namespace

// --------------------------------------------------------------------------

Bar2D BarH::MakeBar() const
{
	const float fStart  = m_rc.XAt(NAME_COL) + m_style.fSeparation;
	const float fEnd	  = m_rc.XAt(1.0f - VALUE_COL) - m_style.fSeparation;
	const float fLength = std::max(1.0f, fEnd - fStart);

	// The pointer stands above the bar, so the bar itself is pushed down by
	// half of what the pointer needs -- that is what leaves the pair centred.
	const float fCY = m_rc.CY() + m_style.fPointer / 2.0f;

	return Bar2D(Vec2D(fStart, fCY), Vec2D(fLength, 0.0f), m_style.fThickness);
}

// --------------------------------------------------------------------------

void BarH::DrawStatic(Painter& P) const
{
	DrawPlate(P, m_rc, m_style);
	DrawBands(P, MakeBar(), *m_pPar, m_style);

	P.Flush();

	P.SetFont(m_style.fontTitle);
	const std::string ssName = FitName(P, *m_pPar, m_rc.fW * NAME_COL - m_style.fMargin);
	P.DrawTextAnchored(m_rc.Left() + m_style.fMargin, m_rc.CY(), ssName.c_str(), 0.0f, 0.5f, m_style.uTitleRgb);
}

// --------------------------------------------------------------------------

void BarH::DrawDynamic(Painter& P) const
{
	const Bar2D bar  = MakeBar();
	const float fRel = Relative(*m_pPar);

	// The segment runs right across the screen, so its positive normal points
	// down and -1 is the top edge. The pointer stands just off it, facing
	// back down at the band.
	const Vec2D pt = bar.GetPointHT(fRel, -1.0f);
	DrawPointer(P, pt.GetX(), pt.GetY() - 2.0f, 90.0f, ValueColor(*m_pPar, m_style), m_style);

	P.Flush();

	DrawValueAndUnit(P, *m_pPar, m_rc.Right() - m_style.fMargin, m_rc.CY(), m_style);
}

// --------------------------------------------------------------------------

} // namespace item
