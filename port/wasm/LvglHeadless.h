/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// LVGL with nothing in front of it, and a canvas to draw on.
//
// The simulator gets its display from lv_sdl_window_create() and the board
// from the BSP; neither is available here and neither is wanted. What this
// build needs of a display is only that one exist: lv_obj_create() wants a
// screen to put the object on, and a screen belongs to a display. Nothing is
// ever refreshed -- lv_timer_handler() is never called -- so the display is
// one pixel wide and its flush callback does nothing.
//
// The canvas is the real subject. LVGL draws into whatever buffer a canvas is
// pointed at, so the buffer is where the picture ends up and the canvas is the
// handle the drawing code takes. It is ARGB8888 and must stay ARGB8888:
// lv_draw_sw_vector renders straight into that format and allocates a
// full-area temporary and blends back for any other -- see "The canvas must
// stay ARGB8888" in CLAUDE.md.
//
// One Surface is kept per Renderer and resized when a render asks for a size
// it is not already holding. An editor drags one widget at a time and asks for
// the same size over and over, so that reallocation is rare; making a fresh
// buffer per call would mean a malloc and a free of a few hundred kB per
// frame of a drag.

#include "lvgl.h"
#include "lvgl_cpp.h"

#include <cstdint>
#include <optional>

namespace wasm {

// Bring LVGL up. Safe to call more than once; the second and later calls do
// nothing. Returns false if LVGL would not start, which leaves the module
// unable to draw anything.
bool LvglStart();

// A canvas and the buffer it draws into, resized on demand.
class Surface
{
public:
	Surface() = default;

	// Point the canvas at a buffer of this size, allocating a new one unless
	// the current one already matches. Returns false when there was no room.
	bool Resize(int32_t iW, int32_t iH);

	bool IsReady() const { return m_buf.has_value() && m_canvas.has_value(); }

	int32_t GetWidth() const { return m_iW; }
	int32_t GetHeight() const { return m_iH; }

	lvgl::Canvas&	GetCanvas() { return *m_canvas; }
	lv_draw_buf_t* GetDrawBuf() { return m_buf->raw(); }

private:
	// Declared buffer first, canvas second, so that destruction -- which runs
	// in reverse -- takes the canvas down before the buffer it is pointed at.
	std::optional<lvgl::DrawBuf> m_buf;
	std::optional<lvgl::Canvas>  m_canvas;

	int32_t m_iW = 0;
	int32_t m_iH = 0;
};

} // namespace wasm
