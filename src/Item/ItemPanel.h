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

class Panel
{
public:
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
	void Rebuild();

	// One row as the item that draws it, or nullptr when the row names a
	// parameter this build does not hold -- a layout written for a whole panel
	// will name plenty of them, which is not an error.
	std::unique_ptr<Base> MakeItem(const Config& cfg) const;

	// The parameter this row names, or nullptr when the container does not
	// hold it. The pointer an item keeps is into the container, which only
	// ever has parameters applied to it in place, never re-inserted.
	static const Param* FindParameter(::can::Id eId);

private:
	Style					  m_style;
	std::vector<Config> m_vItems;

	// The items those rows were turned into, in the order they are drawn.
	std::vector<std::unique_ptr<Base>> m_vDrawn;

	// Where the static half lives between frames.
	std::optional<lvgl::DrawBuf> m_bg;
	bool								  m_bStaticDone = false;
};

} // namespace item
