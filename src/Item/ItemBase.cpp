/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The primitives more than one kind of panel item is assembled from.

#include "ItemBase.h"

#include "Avio/Format/AvioFormat.h"
#include "Parameter/Param.h"
#include "Parameter/ParamFormat.h"

#include <algorithm>
#include <cctype>

namespace item {

namespace {

	// Upper case, ASCII only. The parameter names this product carries are
	// ASCII and Common's own panels letter a title in capitals.
	std::string Upper(std::string ss)
	{
		for(char& c : ss)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return ss;
	}

	// The bare scale, the full length of the arc or the bar. Drawn under the
	// coloured bands and never on its own, which is why it is not declared
	// with them in the header.
	void DrawTrack(Painter& P, const Arc2D& arc, const Style& style)
	{
		if(style.uTrackRgb == NO_FILL)
			return;

		P.SetPen(style.uTrackRgb, arc.GetThickness());
		P.AppendArc(
			arc.GetCenter().GetX(),
			arc.GetCenter().GetY(),
			arc.GetRadius(),
			common::Deg(arc.GetStartAngle()),
			common::Deg(arc.GetSpan())
		);
		P.Emit();
	}

	void DrawTrack(Painter& P, const Bar2D& bar, const Style& style)
	{
		if(style.uTrackRgb == NO_FILL)
			return;

		const Vec2D p1 = bar.GetPointHT(0.0f, -1.0f);
		const Vec2D p2 = bar.GetPointHT(0.0f, 1.0f);
		const Vec2D p3 = bar.GetPointHT(1.0f, 1.0f);
		const Vec2D p4 = bar.GetPointHT(1.0f, -1.0f);

		P.SetBrush(style.uTrackRgb);
		P.MoveTo(p1.GetX(), p1.GetY());
		P.LineTo(p2.GetX(), p2.GetY());
		P.LineTo(p3.GetX(), p3.GetY());
		P.LineTo(p4.GetX(), p4.GetY());
		P.ClosePath();
		P.Emit();
	}

} // namespace

// --------------------------------------------------------------------------

float LineHeight(Painter& P, const Font& font)
{
	P.SetFont(font);
	return P.TextSize("8").fH;
}

// --------------------------------------------------------------------------

void DrawPlate(Painter& P, const Rect& rc, const Style& style)
{
	const float fR = 8.0f;

	if(style.uPlateRgb != NO_FILL) {
		P.SetBrush(style.uPlateRgb);
		P.GetPath().append_rect(rc.fX, rc.fY, rc.fW, rc.fH, fR, fR);
		P.Emit();
	}

	if(style.uBorderRgb != NO_FILL) {
		P.SetPen(style.uBorderRgb, 1.5f);
		P.GetPath().append_rect(rc.fX, rc.fY, rc.fW, rc.fH, fR, fR);
		P.Emit();
	}
}

// --------------------------------------------------------------------------

std::string FitName(const Painter& P, const Param& par, float fMaxWidth)
{
	using ::parameter::NameLength;

	// Longest first: the tiny name always "fits" and is the last resort, the
	// same order lasky::utils::FindOptimalNameLength() walks.
	for(const NameLength eNL : {NameLength::Long, NameLength::Short, NameLength::Tiny}) {
		std::string ss = Upper(par.GetName(eNL));
		if(ss.empty())
			continue;
		if(P.TextSize(ss.c_str()).fW <= fMaxWidth || eNL == NameLength::Tiny)
			return ss;
	}
	return std::string();
}

// --------------------------------------------------------------------------

void DrawBands(Painter& P, const Arc2D& arc, const Param& par, const Style& style)
{
	DrawTrack(P, arc, style);

	const ::parameter::Bands& bands = par.GetBands();
	if(bands.IsValid() == false)
		return;

	const float fSaDeg = common::Deg(arc.GetStartAngle());
	const float fSpDeg = common::Deg(arc.GetSpan());
	const float fCX	 = arc.GetCenter().GetX();
	const float fCY	 = arc.GetCenter().GetY();

	for(int i = 0; i < bands.GetCount(); ++i) {
		const ::parameter::Color eColor = bands.GetColor(i);
		if(eColor == ::parameter::Color::NoColor)
			continue;

		const auto rel = bands.GetRelativeRange(i);
		P.SetPen(style.colors.GetColor(eColor), arc.GetThickness());
		P.AppendArc(fCX, fCY, arc.GetRadius(), fSaDeg + rel.GetLow() * fSpDeg, rel.GetSpan() * fSpDeg);
		P.Emit();
	}
}

// --------------------------------------------------------------------------

void DrawBands(Painter& P, const Bar2D& bar, const Param& par, const Style& style)
{
	DrawTrack(P, bar, style);

	const ::parameter::Bands& bands = par.GetBands();
	if(bands.IsValid() == false)
		return;

	for(int i = 0; i < bands.GetCount(); ++i) {
		const ::parameter::Color eColor = bands.GetColor(i);
		if(eColor == ::parameter::Color::NoColor)
			continue;

		const auto rel = bands.GetRelativeRange(i);

		// The four corners, taken off the thick segment itself, so the same
		// code serves a bar running in any direction.
		const Vec2D p1 = bar.GetPointHT(rel.GetLow(), -1.0f);
		const Vec2D p2 = bar.GetPointHT(rel.GetLow(), 1.0f);
		const Vec2D p3 = bar.GetPointHT(rel.GetHigh(), 1.0f);
		const Vec2D p4 = bar.GetPointHT(rel.GetHigh(), -1.0f);

		P.SetBrush(style.colors.GetColor(eColor));
		P.MoveTo(p1.GetX(), p1.GetY());
		P.LineTo(p2.GetX(), p2.GetY());
		P.LineTo(p3.GetX(), p3.GetY());
		P.LineTo(p4.GetX(), p4.GetY());
		P.ClosePath();
		P.Emit();
	}
}

// --------------------------------------------------------------------------

void DrawPointer(Painter& P, float fX, float fY, float fAngleDeg, ::gui::ARGB argb, const Style& style)
{
	const float fL = style.fPointer;
	const float fW = style.fPointerHalfWidth;

	P.SetBrush(argb);
	P.PushTransform(fX, fY, fAngleDeg);

	// Apex at the origin, base fL away along the local +x axis -- which the
	// transform has already turned to face fAngleDeg.
	P.MoveTo(0.0f, 0.0f);
	P.LineTo(fL, -fW);
	P.LineTo(fL, fW);
	P.ClosePath();

	// Inside the bracket: ThorVG captures the matrix as the path is taken.
	P.Emit();
	P.PopTransform();
}

// --------------------------------------------------------------------------

::gui::ARGB ValueColor(const Param& par, const Style& style)
{
	return style.colors.GetColor(par.GetBands().GetColorType(par.GetValueSystem()));
}

// --------------------------------------------------------------------------

float Relative(const Param& par, int iIndex)
{
	return common::GetMinMax(par.GetBands().GetRelative(par.GetValueSystem(iIndex)), 0.0f, 1.0f);
}

// --------------------------------------------------------------------------

std::string ValueText(const Param& par, int iIndex)
{
	return ::parameter::Format(par.GetValueUser(iIndex), par.GetFunction(), par.GetUnitKeyUser());
}

// --------------------------------------------------------------------------

std::string UnitText(const Param& par)
{
	using avio::format::UnitExtension;

	// Common never drew a glyph for rpm or percent and the formatter answers
	// an empty string rather than falling back by itself.
	std::string ss = avio::format::ToString(par.GetUnitKeyUser(), UnitExtension::Glyph);
	if(ss.empty())
		ss = avio::format::ToString(par.GetUnitKeyUser(), UnitExtension::Signature);
	return ss;
}

// --------------------------------------------------------------------------

float ValueAboveUnitHeight(Painter& P, const Style& style)
{
	const float fValue = LineHeight(P, style.fontValue);
	const float fUnit	 = LineHeight(P, style.fontUnit);

	return fValue + style.fSeparation / 2.0f + fUnit;
}

// --------------------------------------------------------------------------

void DrawValueAboveUnit(Painter& P, const Param& par, float fCX, float fTop, const Style& style)
{
	const ::gui::ARGB argb = ValueColor(par, style);

	const float fH = LineHeight(P, style.fontValue);
	P.DrawTextAnchored(fCX, fTop, ValueText(par).c_str(), 0.5f, 0.0f, argb);

	const std::string ssUnit = UnitText(par);
	if(ssUnit.empty())
		return;

	P.SetFont(style.fontUnit);
	P.DrawTextAnchored(fCX, fTop + fH + style.fSeparation / 2.0f, ssUnit.c_str(), 0.5f, 0.0f, argb);
}

// --------------------------------------------------------------------------

void DrawValueAndUnit(Painter& P, const Param& par, float fRight, float fCY, const Style& style)
{
	const ::gui::ARGB argb	 = ValueColor(par, style);
	const std::string ssUnit = UnitText(par);

	float fX = fRight;
	if(ssUnit.empty() == false) {
		P.SetFont(style.fontUnit);
		P.DrawTextAnchored(fX, fCY, ssUnit.c_str(), 1.0f, 0.5f, argb);
		fX -= P.TextSize(ssUnit.c_str()).fW + style.fSeparation;
	}

	P.SetFont(style.fontValue);
	P.DrawTextAnchored(fX, fCY, ValueText(par).c_str(), 1.0f, 0.5f, argb);
}

// --------------------------------------------------------------------------

} // namespace item
