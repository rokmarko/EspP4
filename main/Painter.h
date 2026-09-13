/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2019 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *   Writen by:                                                            *
 *      Rok Markovic [rok.markovic@kanardia.eu]                            *
 *                                                                         *
 *   Status: Open Source                                                   *
 *                                                                         *
 *   License: GPL - GNU General Public License                             *
 *                                                                         *
 ***************************************************************************/

#pragma once

// The drawing back end scale::Scale is written against.
//
// Neither LVGL nor Qt appears here: this header names the operations the scale
// needs and nothing else, so the same geometry can be rasterised by ThorVG on
// the panel (PainterTvg.h) or by QPainter on a desktop (PainterQt.h).
//
// The abstraction is a concept rather than a base class on purpose. Every
// primitive is a non-virtual member of a concrete type known when the drawing
// call is instantiated, so the calls inline away and the emitted code is the
// same as a back end written out by hand -- there is no vtable, and no
// indirect call.

// ScaleStyle.h uses std::string for the font family without including <string>;
// it happens to compile in the Qt products, where Qt's headers get there first.
// Include it here rather than patching the shared tree.
#include <string>

#include "Gui/Rgb.h"
#include "Scale/ScaleStyle.h"

#include <concepts>

namespace scale {

// Width and height of a rendered string, in pixels.
struct Size2D
{
	float fW;
	float fH;
};

// Everything scale::Scale asks of a drawing back end.
//
// Three conventions hold across every implementation:
//
//  - **Angles are float degrees in Arc2D sense**: zero at 3 o'clock, growing
//    counter-clockwise on screen. Each back end adapts to its own API. LVGL's
//    vector angles run clockwise; QPainter is inconsistent with itself, as
//    drawArc() measures counter-clockwise while rotate() does not.
//  - **Pen and brush are mutually exclusive**, as in the LVGL descriptor.
//    SetPen() means stroke with no fill, SetBrush() means fill with no stroke.
//    There is no state in which both apply.
//  - **Transforms do not nest.** PushTransform()/PopTransform() bracket one
//    region at a time; the ThorVG back end implements the pop as a reset to
//    identity, not as a stack pop, and a nested push would lose the outer
//    transform.
//
// Path building and emission are separate steps because that is what makes the
// panel fast: strokes sharing a pen accumulate into a single path and rasterise
// in one go. A ThorVG frame on this board costs tens of milliseconds and the
// count of paths is what drives it. Emit() ends one such batch. Flush() is the
// coarser barrier -- see below.
template <typename P>
concept PainterLike = requires(P& p, const P& cp, ::gui::ARGB argb, float f, const style::Font& font, const char* psz) {
	// State. Setting one of the two colours clears the other.
	{ p.SetPen(argb, f) } -> std::same_as<void>; // colour, width
	{ p.SetBrush(argb) } -> std::same_as<void>;
	{ p.SetFont(font) } -> std::same_as<void>;

	// Path building, in the current transform's space.
	{ p.MoveTo(f, f) } -> std::same_as<void>;
	{ p.LineTo(f, f) } -> std::same_as<void>;
	{ p.ClosePath() } -> std::same_as<void>;
	{ p.AppendArc(f, f, f, f, f) } -> std::same_as<void>; // cx,cy,r,start,span
	{ p.AppendCircle(f, f, f) } -> std::same_as<void>; // cx,cy,r

	// Transform. Strictly non-nesting; Emit() must run inside the bracket,
	// because ThorVG captures the matrix when the path is handed over.
	{ p.PushTransform(f, f, f) } -> std::same_as<void>;	 // tx,ty,rotDeg
	{ p.PopTransform() } -> std::same_as<void>;

	// Text. Always white, whatever the pen is -- both back ends force it, which
	// is what keeps labels legible after a coloured dash leaves its pen behind.
	// DrawTextCentred() places the centre of the text box at the given point.
	{ cp.TextSize(psz) } -> std::same_as<Size2D>;
	{ p.DrawTextCentred(f, f, psz) } -> std::same_as<void>;

	// Batching. Emit() strokes or fills whatever has been built since the last
	// one, per the current pen or brush, and clears the path.
	//
	// Flush() is an ordering barrier between vector output and text output. On
	// LVGL the two go through different pipelines -- ThorVG has no text API, so
	// labels are lv_draw_label tasks -- and the layer runs its tasks in the
	// order they were added, so vector work must be queued before any text that
	// should sit on top of it. Qt has one immediate-mode pipeline and no such
	// constraint, so its Flush() is empty and costs nothing.
	{ p.Emit() } -> std::same_as<void>;
	{ p.Flush() } -> std::same_as<void>;
};

} // namespace scale
