/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "ItemArc.h"

#include "Parameter/Param.h"

#include <algorithm>

namespace item {

namespace {

	// The arc's bounding box, in radii: two across, and a full radius above
	// the centre plus sin 45 below it, where the two ends stop.
	constexpr float BOX_W = 2.0f;
	constexpr float BOX_H = 1.707f;

	// Start at the lower left and sweep 270 degrees the short way round, so
	// the gap lands at the bottom. Arc2D counts counter-clockwise, hence the
	// negative span -- the same pair VectorScene's round tachometer uses.
	constexpr float START_DEG = 225.0f;
	constexpr float SPAN_DEG  = -270.0f;

} // namespace

// --------------------------------------------------------------------------

Arc2D Arc::MakeArc(Painter& P) const
{
	const float fTopArc = m_style.fMargin + LineHeight(P, m_style.fontTitle) + m_style.fSeparation;

	// What has to fit outside the band itself: half its width, plus the
	// pointer that rides just off it.
	const float fOuter = m_style.fThickness / 2.0f + m_style.fPointer + 3.0f;

	const float fBoxW = m_rc.fW - 2.0f * m_style.fMargin;
	const float fBoxH = m_rc.fH - fTopArc - m_style.fMargin;

	const float fRadius = std::max(1.0f, std::min((fBoxW - 2.0f * fOuter) / BOX_W, (fBoxH - 2.0f * fOuter) / BOX_H));

	// The fitted box, centred horizontally and sitting at the top of what is
	// left under the title.
	const float fCX = m_rc.CX();
	const float fCY = m_rc.fY + fTopArc + (fBoxH - (BOX_H * fRadius + 2.0f * fOuter)) / 2.0f + fOuter + fRadius;

	Arc2D arc(Vec2D(fCX, fCY), fRadius, common::Rad(START_DEG), common::Rad(SPAN_DEG), m_style.fThickness);
	// Screen y grows downward while Arc2D angles grow counter-clockwise.
	arc.SetYReversed(true);
	return arc;
}

// --------------------------------------------------------------------------

void Arc::DrawStatic(Painter& P) const
{
	DrawPlate(P, m_rc, m_style);

	const Arc2D arc = MakeArc(P);
	DrawBands(P, arc, *m_pPar, m_style);

	// Vector work first, then the lettering: one layer runs its tasks in the
	// order they were added, and a title has to land on top of the plate.
	P.Flush();

	P.SetFont(m_style.fontTitle);
	const std::string ssName = FitName(P, *m_pPar, m_rc.fW - 2.0f * m_style.fMargin);
	P.DrawTextAnchored(m_rc.CX(), m_rc.fY + m_style.fMargin, ssName.c_str(), 0.5f, 0.0f, m_style.uTitleRgb);
}

// --------------------------------------------------------------------------

void Arc::DrawDynamic(Painter& P) const
{
	const Arc2D arc  = MakeArc(P);
	const float fRel = Relative(*m_pPar);

	// Apex just off the band's outer edge, base further out still, so the
	// triangle points back at the colour it is standing on.
	const Vec2D pt		 = arc.GetPointR(fRel, arc.GetRadius() + m_style.fThickness / 2.0f + 2.0f);
	const float fAngle = common::Deg(arc.GetAngleBounded(fRel));
	DrawPointer(P, pt.GetX(), pt.GetY(), fAngle, ValueColor(*m_pPar, m_style), m_style);

	P.Flush();

	// The readout goes in the gap the arc leaves under itself.
	const float fH = ValueAboveUnitHeight(P, m_style);
	DrawValueAboveUnit(P, *m_pPar, m_rc.CX(), m_rc.Bottom() - m_style.fMargin - fH, m_style);
}

// --------------------------------------------------------------------------

} // namespace item
