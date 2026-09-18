/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// A screenful of items, drawn in two passes.
//
// The panel is a list of Config rows -- a kind, a parameter and a box -- and
// nothing else. That is what makes it configurable: a layout is data, and
// MockupLayout() is one hardcoded answer standing in until the real one comes
// out of the settings store.
//
// Rendering is the split ItemBase.h describes, made real:
//
//   1. the static half is drawn once, into a background buffer of its own --
//      the plates, the coloured bands, every title;
//   2. every frame, that whole buffer is blitted over the canvas, and only
//      the pointers and the readouts are drawn on top of it.
//
// A ThorVG frame on this panel costs tens of milliseconds and the count of
// paths is what drives it, so the second pass is a small fraction of what
// drawing the panel outright would cost. The price is one more ARGB8888
// buffer the size of the canvas, which LVGL's allocator places in PSRAM.
//
// Invalidate() is what a changed layout, a changed style or a parameter
// pushed over the bus has to call: the bands live in the background.
//
// A Config row is the configuration; an item::Base is what that row is turned
// into, once, when the layout or the style is set. Each item carries its own
// parameter, box and style from then on, so the per-frame loop is a walk over
// item::Base pointers with nothing to look up and nothing to branch on.

#include "ItemBase.h"

#include "ItemArc.h"
#include "ItemBarH.h"
#include "ItemBarV.h"
#include "ItemValue.h"

#include "CanAerospace/CanIds.h"

#include "lvgl.h"
#include "lvgl_cpp.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace parameter { class ParameterContainer; }

namespace item {

enum class Kind
{
	Arc,
	BarH,
	BarV,
	Value
};

// One item on the panel: what to draw, for which parameter, where.
struct Config
{
	Kind		 eKind = Kind::Value;
	::can::Id eId	 = ::can::Id::Invalid;
	Rect		 rc;
};

// One row as the item that draws it, or nullptr when parameters does not hold
// that row's parameter.
//
// The container is handed in rather than reached for. Without that this file
// would call app::GetModel(), and an item -- which needs nothing but a
// parameter, a box and a painter -- would drag the flight model, the CAN stack
// and the NOD in behind it. That is what lets the Kaledi layout editor build
// the very same items out of a container of its own (port/wasm).
//
// This is the only place a Kind is ever branched on. Panel::Rebuild() is a walk
// over it; the layout editor calls it for one row at a time, which is the whole
// of what it needs from this header.
std::unique_ptr<Base>
MakeItem(const ::parameter::ParameterContainer& parameters, const Style& style, const Config& cfg);

class Panel
{
public:
	// parameters is where every row's can::Id is looked up, and the only thing
	// this panel is told about the world. It has to outlive the panel: an item
	// keeps a pointer straight into the container, which stays good because
	// nothing is ever inserted after the container is built -- parameters are
	// only ever applied to in place.
	explicit Panel(const ::parameter::ParameterContainer& parameters) :
		m_pParameters(&parameters)
	{}

	// Allocate the background buffer. iW and iH are the canvas the panel is
	// rendered onto, and the buffer matches it exactly -- the blit is a
	// whole-buffer copy, not a scaled one.
	//
	// Returns false when there was no room, in which case the panel draws
	//         nothing and the scene should skip it.
	bool Build(int32_t iW, int32_t iH);

	bool IsReady() const { return m_bg.has_value(); }

	void								SetLayout(std::vector<Config> vItems);
	const std::vector<Config>& GetLayout() const { return m_vItems; }

	void			 SetStyle(const Style& style);
	const Style& GetStyle() const { return m_style; }

	// Draw the static half again on the next frame.
	void Invalidate() { m_bStaticDone = false; }

	// One frame onto canvas: the background blitted whole, then the moving
	// half over it. The static half is drawn first if it is stale.
	//
	// pFront is the canvas's own draw buffer, which the static pass swaps
	// away from and has to put back.
	void Render(lvgl::Canvas& canvas, lv_draw_buf_t* pFront, uint32_t uBgRgb);

	// The hardcoded mockup: two bars up the sides, an arc across the top, two
	// horizontal bars under it and a pair of bare readouts at the foot. Every
	// kind appears at least once, which is the point of it.
	static std::vector<Config> MockupLayout(float fW, float fH);

private:
	void RenderStatic(lvgl::Canvas& canvas, lv_draw_buf_t* pFront, uint32_t uBgRgb);
	void DrawAll(lvgl::Canvas& canvas, bool bStatic);

	// Turn the configuration into the items that draw it. Called whenever the
	// layout or the style changes, and never per frame.
	//
	// A row naming a parameter this build does not hold is dropped here, not
	// an error -- a layout written for a whole panel will name plenty of them.
	void Rebuild();

private:
	const ::parameter::ParameterContainer* m_pParameters;

	Style					  m_style;
	std::vector<Config> m_vItems;

	// The items those rows were turned into, in the order they are drawn.
	std::vector<std::unique_ptr<Base>> m_vDrawn;

	// Where the static half lives between frames.
	std::optional<lvgl::DrawBuf> m_bg;
	bool								  m_bStaticDone = false;
};

} // namespace item
