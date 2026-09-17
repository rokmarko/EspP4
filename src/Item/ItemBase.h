/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// What every panel item is made of.
//
// An item is one parameter drawn inside one rectangle -- an arc, a bar, a bare
// readout. It is the same idea as `lasky::utils::Arc` and its siblings in
// Public/Nesis, and the split is the one that matters there too:
//
//   DrawStatic()   everything that only changes when the layout or the
//                  parameter's bands change: the title, the coloured bands,
//                  the frame. It is rendered once, into a background buffer.
//   DrawDynamic()  everything that moves: the pointer and the readout. It is
//                  drawn on top of the blitted background, every frame.
//
// That is what makes a panel of a dozen items affordable on this board. A
// ThorVG frame costs tens of milliseconds and the count of paths is what
// drives it; the bands and the lettering are most of those paths and none of
// them change between frames.
//
// Nesis needs two painters for the same split -- a QPainter for the pixmap and
// a PainterGL for the live half. Here both halves go through one PainterTvg,
// because what separates them is which buffer LVGL is pointed at, not which
// API draws.
//
// `item::Base` is those two calls and nothing else, so that a panel can hold
// items without knowing what any of them are. What the kinds have in common is
// below it as free functions, and what only one kind needs is that kind's own
// business -- an arc works out where its centre goes, a bar does not care.
//
// Angles follow the rest of this project: float degrees in Arc2D sense, zero
// at 3 o'clock, growing counter-clockwise on screen.

#include "KanardiaCommon.h"

#include "PainterTvg.h"

#include "Geometry/Arc2D.h"
#include "Geometry/LineSegment2D.h"
#include "Parameter/ParamBands.h"
#include "Parameter/ParamColors.h"
#include "Scale/ScaleStyle.h"
#include "Unit/UnitKeys.h"

#include <string>

namespace parameter { class Parameter; }

namespace item {

using Painter = ::scale::PainterTvg;
using Vec2D	  = ::geometry::fVector2D;
using Arc2D	  = ::geometry::ThickArc2D<float>;
using Bar2D	  = ::geometry::ThickLineSegment2D<float>;
using Font	  = ::scale::style::Font;
using Param	  = ::parameter::Parameter;

// Not a colour: what a Style field holds when nothing should be drawn for it.
// 24-bit RGB cannot spell this, so it can never collide with a real one.
constexpr uint32_t NO_FILL = 0xFFFFFFFFu;

// The box an item is given, in canvas pixels.
//
// Not a QRectF and not one of Common's: this is the one type the panel layout
// is written in, and it carries only what the items ask of it.
struct Rect
{
	float fX = 0.0f;
	float fY = 0.0f;
	float fW = 0.0f;
	float fH = 0.0f;

	float Left() const { return fX; }
	float Top() const { return fY; }
	float Right() const { return fX + fW; }
	float Bottom() const { return fY + fH; }
	float CX() const { return fX + fW / 2.0f; }
	float CY() const { return fY + fH / 2.0f; }

	// The point at the relative position: 0 is the left or the top edge.
	float XAt(float fRel) const { return fX + fRel * fW; }
	float YAt(float fRel) const { return fY + fRel * fH; }

	Rect Inset(float fD) const { return Rect{fX + fD, fY + fD, fW - 2.0f * fD, fH - 2.0f * fD}; }
};

// How an item is lettered, spaced and coloured.
//
// One of these is shared by every item on a panel, so a panel reads as one
// instrument rather than as a row of unrelated widgets. Sizes are the ones the
// build generated -- PainterTvg::CreateFont() snaps to the nearest.
//
// Every item keeps its own copy and every shared primitive below is handed
// one, which is what lets Base stay an interface and nothing else.
struct Style
{
	Font fontTitle{"Kanardia", 16, false};
	Font fontValue{"Kanardia", 24, false};
	Font fontUnit{"Kanardia", 16, false};
	// Between a title and what it labels, and between a value and its unit.
	float fSeparation = 5.0f;
	// Between the item's box and anything drawn in it.
	float fMargin = 6.0f;
	// Width of the coloured band, across the arc or the bar.
	float fThickness = 13.0f;
	// The pointer riding that band: how far it reaches and how wide its base
	// is. It sits outside the band and points back at it.
	float fPointer				= 13.0f;
	float fPointerHalfWidth = 7.0f;
	// The plate each item sits on, and its outline. Either can be NO_FILL,
	// which is how an item asks for no plate or no outline at all.
	uint32_t uPlateRgb  = 0x0E1424;
	uint32_t uBorderRgb = 0x1E2A44;
	// The bare scale under the coloured bands. Without it a parameter whose
	// bands carry no colour -- an altitude, a fuel total -- would draw no
	// scale at all, and the pointer would ride on nothing.
	uint32_t uTrackRgb = 0x243049;
	// The colour a title is lettered in. Values take theirs from the band
	// the needle is standing in.
	uint32_t uTitleRgb = 0x9FB6E0;
	// What each parameter::Color is drawn in. Common's own palette unless a
	// panel says otherwise.
	::parameter::gui::Colors colors;
};

// -----------------------------------------------------------------------
//  The interface
// -----------------------------------------------------------------------

// What the panel knows about an item, and the whole of it.
//
// Two calls, taking only what they draw into. An item is built for one row of
// the layout and keeps that row's parameter, box and style for its whole life,
// so there is nothing left to hand it per frame -- and nothing the panel has
// to know about the kind it is holding.
//
// Everything else an item needs it either owns or reaches for below; none of
// it belongs here, because none of it is what a panel asks an item for.
class Base
{
public:
	virtual ~Base() = default;

	// An item is built once, for one row, and owned through this interface.
	// Copying one would slice it.
	Base(const Base&)				  = delete;
	Base& operator=(const Base&) = delete;

	// Everything that only changes when the layout, the style or the
	// parameter's bands change. Rendered once, into the background buffer.
	virtual void DrawStatic(Painter& P) const = 0;

	// Everything that moves. Drawn on top of that background, every frame.
	virtual void DrawDynamic(Painter& P) const = 0;

protected:
	Base() = default;
};

// -----------------------------------------------------------------------
//  Shared primitives
//
//  Free functions rather than members of Base: an item is not a kind of
//  toolbox, and each of these is wanted by two or three of the four kinds but
//  never by all of them. Anything only one kind needs lives in that kind.
// -----------------------------------------------------------------------

// Set font on P and answer the height of one line in it. Every item measures
// its own layout this way, so they all agree on what a title costs.
float LineHeight(Painter& P, const Font& font);

// The plate an item sits on: a rounded-off rectangle in the plate colour,
// outlined in the border colour. Either is skipped when its Style field is
// NO_FILL.
void DrawPlate(Painter& P, const Rect& rc, const Style& style);

// The parameter's name, upper-cased, in the longest of its three forms that
// still fits fMaxWidth. Common gives every parameter all three for exactly
// this reason.
//
// The font has to be set on P before this is called -- it is what decides what
// fits.
std::string FitName(const Painter& P, const Param& par, float fMaxWidth);

// The coloured bands, as arcs on arc or as rectangles along bar, over a bare
// track running the whole length.
//
// Bands with no colour are skipped, the way Common's own scale skips them:
// that is what leaves a tachometer uncoloured below idle. The track is what
// keeps a parameter whose bands are all uncoloured from drawing nothing.
void DrawBands(Painter& P, const Arc2D& arc, const Param& par, const Style& style);
void DrawBands(Painter& P, const Bar2D& bar, const Param& par, const Style& style);

// A triangle with its apex at (fX, fY), reaching fPointer back along
// fAngleDeg. Pointing at the band it stands on, in other words, from whichever
// side the item put it.
void DrawPointer(Painter& P, float fX, float fY, float fAngleDeg, ::gui::ARGB argb, const Style& style);

// The colour the value is lettered in: the band it is standing in, so a
// readout goes yellow and then red with the scale behind it.
::gui::ARGB ValueColor(const Param& par, const Style& style);

// Where the parameter is, as 0..1 along its own band range. Taken in the
// system unit, which is what the bands are stored in -- a relative position
// needs no conversion and this is the per-frame path.
float Relative(const Param& par, int iIndex = 0);

// The number, formatted the way every other Kanardia product formats that
// function, and the unit's own glyph. Kept apart because the two are lettered
// in different sizes.
std::string ValueText(const Param& par, int iIndex = 0);
std::string UnitText(const Param& par);

// The height of a value set above its unit, which is what an arc and a
// vertical bar reserve at the bottom of their box.
float ValueAboveUnitHeight(Painter& P, const Style& style);

// A value with its unit under it, centred on fCX, the pair filling that height
// downwards from fTop.
void DrawValueAboveUnit(Painter& P, const Param& par, float fCX, float fTop, const Style& style);

// A value with its unit beside it, the pair ending at fRight and centred
// vertically on fCY. That is what a horizontal bar and a bare readout want,
// where height is the scarce direction.
void DrawValueAndUnit(Painter& P, const Param& par, float fRight, float fCY, const Style& style);

} // namespace item
