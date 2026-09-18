/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// Bringing LVGL up with no display worth the name.

#include "LvglHeadless.h"

#include "Platform.h"

#include <emscripten/emscripten.h>

namespace wasm {

namespace {

	constexpr const char* TAG = "lvgl";

	bool			  g_bStarted = false;
	lv_display_t* g_pDisplay = nullptr;

	// The screen every canvas is parented on. Taken once, after the display
	// exists, and never loaded or drawn -- it is somewhere to put an object,
	// nothing more.
	std::optional<lvgl::Screen> g_screen;

	// Never called: lv_timer_handler() is what drives a refresh and nothing
	// here calls it. It is set so that the display is not left holding a null
	// one, which is a cheaper answer than reasoning about whether some future
	// LVGL can dispatch a refresh on its own.
	void FlushNothing(lv_display_t* pDisp, const lv_area_t*, uint8_t*)
	{
		lv_display_flush_ready(pDisp);
	}

	// LVGL needs a millisecond clock and a delay. The simulator gets both from
	// lv_sdl_window_create(); there is no LV_TICK_CUSTOM in LVGL 9, so a build
	// with no display driver has to install them itself or every timeout in
	// LVGL sits at zero forever.
	uint32_t TickMs()
	{
		return static_cast<uint32_t>(emscripten_get_now());
	}

	void DelayMs(uint32_t)
	{
		// A browser's main thread cannot block; see PlatformWasm.cpp. Nothing
		// on the render path delays.
	}

} // namespace

// --------------------------------------------------------------------------

bool LvglStart()
{
	if(g_bStarted)
		return true;

	lv_init();
	lv_tick_set_cb(TickMs);
	lv_delay_set_cb(DelayMs);

	g_pDisplay = lv_display_create(1, 1);
	if(g_pDisplay == nullptr) {
		APP_LOGE(TAG, "no display; nothing can be drawn");
		return false;
	}

	lv_display_set_flush_cb(g_pDisplay, FlushNothing);

	// And deliberately no lv_display_set_buffers(). Nothing here is ever
	// refreshed, so there is nothing for a display buffer to hold -- an item is
	// drawn into the canvas's own buffer by init_layer()/finish_layer(), which
	// does not go through the display at all.
	//
	// It is worth spelling out because the obvious gesture -- one pixel, to
	// keep LVGL happy -- hangs. lv_display_set_buffers() sizes its rows against
	// the display's render format, which at LV_COLOR_DEPTH 32 is four bytes a
	// pixel, while sizeof(lv_color_t) is three; a buffer that small works out
	// to zero rows and the call does not come back.

	g_screen.emplace(lvgl::Screen::active());

	g_bStarted = true;
	APP_LOGI(TAG, "LVGL %d.%d.%d up, headless", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
	return true;
}

// --------------------------------------------------------------------------

bool Surface::Resize(int32_t iW, int32_t iH)
{
	if(iW <= 0 || iH <= 0)
		return false;

	if(IsReady() && m_iW == iW && m_iH == iH)
		return true;

	if(LvglStart() == false)
		return false;

	// The canvas goes first: it is pointed at the buffer below and must not
	// outlive it, and Resize() is also how a failed allocation leaves this
	// object -- empty rather than pointed at freed memory.
	m_canvas.reset();
	m_buf.reset();
	m_iW = 0;
	m_iH = 0;

	m_buf.emplace(static_cast<uint32_t>(iW), static_cast<uint32_t>(iH), lvgl::ColorFormat::ARGB8888);
	if(m_buf->raw() == nullptr) {
		APP_LOGE(TAG, "no room for a %dx%d ARGB8888 buffer (%d kB)", iW, iH, iW * iH * 4 / 1024);
		m_buf.reset();
		return false;
	}

	m_canvas.emplace(*g_screen);
	m_canvas->set_draw_buf(m_buf->raw());

	m_iW = iW;
	m_iH = iH;
	return true;
}

// --------------------------------------------------------------------------

} // namespace wasm
