/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "ItemBarV.h"

#include "Parameter/Param.h"

#include <algorithm>

namespace item {

// --------------------------------------------------------------------------

Bar2D BarV::MakeBar(Painter& P) const
{
	const float fTitleH = LineHeight(P, m_style.fontTitle);
	const float fValueH = ValueAboveUnitHeight(P, m_style);

	const float fTop	  = m_rc.fY + m_style.fMargin + fTitleH + m_style.fSeparation;
	const float fBottom = m_rc.Bottom() - m_style.fMargin - fValueH - m_style.fSeparation;
	const float fLength = std::max(1.0f, fBottom - fTop);

	// The pointer rides the left flank, so the bar itself is pushed right by
	// half of what the pointer needs -- that is what leaves the pair centred.
	const float fCX = m_rc.CX() + m_style.fPointer / 2.0f;

	// Start at the bottom, run upwards: relative 0 is the band range's low
	// end, which is where a bar reads from.
	return Bar2D(Vec2D(fCX, fBottom), Vec2D(0.0f, -fLength), m_style.fThickness);
}

// --------------------------------------------------------------------------

void BarV::DrawStatic(Painter& P) const
{
	DrawPlate(P, m_rc, m_style);
	DrawBands(P, MakeBar(P), *m_pPar, m_style);

	P.Flush();

	P.SetFont(m_style.fontTitle);
	const std::string ssName = FitName(P, *m_pPar, m_rc.fW - 2.0f * m_style.fMargin);
	P.DrawTextAnchored(m_rc.CX(), m_rc.fY + m_style.fMargin, ssName.c_str(), 0.5f, 0.0f, m_style.uTitleRgb);
}

// --------------------------------------------------------------------------

void BarV::DrawDynamic(Painter& P) const
{
	const Bar2D bar  = MakeBar(P);
	const float fRel = Relative(*m_pPar);

	// The segment runs up the screen, so its positive normal points right and
	// -1 is the left edge. The pointer stands just off it, facing back.
	const Vec2D pt = bar.GetPointHT(fRel, -1.0f);
	DrawPointer(P, pt.GetX() - 2.0f, pt.GetY(), 180.0f, ValueColor(*m_pPar, m_style), m_style);

	P.Flush();

	const float fH = ValueAboveUnitHeight(P, m_style);
	DrawValueAboveUnit(P, *m_pPar, m_rc.CX(), m_rc.Bottom() - m_style.fMargin - fH, m_style);
}

// --------------------------------------------------------------------------

} // namespace item
