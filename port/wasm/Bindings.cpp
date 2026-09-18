/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// What JavaScript sees.
//
// Embind rather than raw extern "C" exports: every call here takes or returns
// a std::string or a typed array, and doing that over _malloc and HEAPU8 would
// put memory management in the editor, at every call site, for no gain.
//
// The names are lowerCamelCase on this side, which is the convention
// JavaScript reads, while the C++ keeps Kanardia's PascalCase. That is the
// same split src/ already makes for LVGL's snake_case.
//
// Binary blobs arrive as std::string because embind maps a JS Uint8Array or
// ArrayBuffer straight onto one, bytes intact, with no interpretation -- a
// flatbuffer holds embedded nulls and survives that unharmed.
//
// A Renderer is a C++ object with a lifetime, so JavaScript has to delete()
// it. That is embind's rule for class_ bindings and there is no way around it;
// an editor holds exactly one for as long as it is open, so it is one call in
// a teardown path.

#include "KalediRenderer.h"

#include <emscripten/bind.h>

EMSCRIPTEN_BINDINGS(kaledi_item)
{
	emscripten::class_<kaledi::Renderer>("Renderer")
		.constructor<>()
		.function("setParameter", &kaledi::Renderer::SetParameter)
		.function("setParameters", &kaledi::Renderer::SetParameters)
		.function("loadDefaults", &kaledi::Renderer::LoadDefaults)
		.function("setValue", &kaledi::Renderer::SetValue)
		.function("getParameters", &kaledi::Renderer::GetParameters)
		.function("setStyle", &kaledi::Renderer::SetStyle)
		.function("render", &kaledi::Renderer::Render)
		.function("getLastError", &kaledi::Renderer::GetLastError);
}
