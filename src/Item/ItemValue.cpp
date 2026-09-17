/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "ItemValue.h"

#include "Parameter/Param.h"

namespace item {

// --------------------------------------------------------------------------

float Value::BlockTop(Painter& P) const
{
	const float fTitleH = LineHeight(P, m_style.fontTitle);
	const float fValueH = LineHeight(P, m_style.fontValue);

	const float fBlockH = fTitleH + m_style.fSeparation + fValueH;
	return m_rc.CY() - fBlockH / 2.0f;
}

// --------------------------------------------------------------------------

void Value::DrawStatic(Painter& P) const
{
	DrawPlate(P, m_rc, m_style);
	P.Flush();

	const float fTop = BlockTop(P);

	P.SetFont(m_style.fontTitle);
	const std::string ssName = FitName(P, *m_pPar, m_rc.fW - 2.0f * m_style.fMargin);
	P.DrawTextAnchored(m_rc.CX(), fTop, ssName.c_str(), 0.5f, 0.0f, m_style.uTitleRgb);
}

// --------------------------------------------------------------------------

void Value::DrawDynamic(Painter& P) const
{
	const float fTop = BlockTop(P);

	const float fTitleH = LineHeight(P, m_style.fontTitle);
	const float fValueH = LineHeight(P, m_style.fontValue);

	// The value and its unit sit side by side on the block's lower line: a
	// readout this small has height to spare in one direction only.
	const float fCY = fTop + fTitleH + m_style.fSeparation + fValueH / 2.0f;
	DrawValueAndUnit(P, *m_pPar, m_rc.Right() - m_style.fMargin, fCY, m_style);
}

// --------------------------------------------------------------------------

} // namespace item
