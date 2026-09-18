/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The platform surface in a browser.
//
// Much shorter than port/esp or port/pc, because this build is much less of
// the product: KANARDIA_ITEM_SOURCES is the panel items and the painter they
// draw through, and the only thing in there that reaches for platform:: at all
// is ItemPanel.cpp's APP_LOG* lines. The rest of Platform.h -- tasks, the
// console, the network, the firmware slot, the CAN port, the blob store, the
// MQTT port -- is not implemented here, because nothing in this build
// references it and a stub that is never called is a stub that is never
// checked.
//
// Two of the four that are here are no-ops on purpose:
//
//  - **the display lock**, because there is one thread. LV_USE_OS is LV_OS_NONE
//    (port/wasm/lv_conf.h) and every call arrives from JavaScript on the same
//    thread that built the module, so there is nothing to serialise against.
//  - **SleepMs()**, because a browser's main thread cannot block. Blocking it
//    freezes the page that is asking for the picture. Nothing in the render
//    path sleeps -- it is a pacing primitive for the model loop, which this
//    build does not have -- so the honest answer is to do nothing and say so
//    rather than to reach for ASYNCIFY and make every call up the stack
//    asynchronous for the sake of a call that never happens.
//
// The heap figures are zeros, as the simulator answers them. A browser tab has
// none of the board's limits and inventing numbers for them would invite
// comparisons that mean nothing.

#include "Platform.h"

#include <emscripten/emscripten.h>

#include <cstdio>

namespace platform {

namespace {

	LogLevel g_eLogLevel = LogLevel::Info;

	char LevelChar(LogLevel eLevel)
	{
		switch(eLevel) {
		case LogLevel::None:	 return ' ';
		case LogLevel::Error: return 'E';
		case LogLevel::Warn:	 return 'W';
		case LogLevel::Info:	 return 'I';
		case LogLevel::Debug: return 'D';
		}
		return '?';
	}

} // namespace

// --------------------------------------------------------------------------

const char* Name()
{
	return "wasm";
}

// --------------------------------------------------------------------------

void Log(LogLevel eLevel, const char* pszTag, const char* pszFmt, ...)
{
	if(static_cast<uint8_t>(eLevel) > static_cast<uint8_t>(g_eLogLevel))
		return;

	char	  szLine[512];
	va_list args;
	va_start(args, pszFmt);
	std::vsnprintf(szLine, sizeof(szLine), pszFmt, args);
	va_end(args);

	// stderr, which emscripten routes to console.error -- so a line from the
	// editor's preview reads in the browser's console the same way a line from
	// the simulator reads in a terminal, and the same eye can scan both.
	const long lMs = static_cast<long>(Micros() / 1000);
	std::fprintf(stderr, "%c (%ld) %s: %s\n", LevelChar(eLevel), lMs, pszTag, szLine);
}

// --------------------------------------------------------------------------

void SetLogLevel(LogLevel eLevel)
{
	g_eLogLevel = eLevel;
}

// --------------------------------------------------------------------------

LogLevel GetLogLevel()
{
	return g_eLogLevel;
}

// --------------------------------------------------------------------------

int64_t Micros()
{
	// emscripten_get_now() is performance.now(), milliseconds since the page
	// loaded, as a double. Monotonic, which is what the contract asks for.
	return static_cast<int64_t>(emscripten_get_now() * 1000.0);
}

// --------------------------------------------------------------------------

void SleepMs(uint32_t)
{
	// Deliberately nothing. See the note at the top of this file.
}

// --------------------------------------------------------------------------

void LockDisplay()
{
	// One thread. See the note at the top of this file.
}

// --------------------------------------------------------------------------

void UnlockDisplay() {}

// --------------------------------------------------------------------------

HeapStats GetHeapStats()
{
	return HeapStats{};
}

// --------------------------------------------------------------------------

} // namespace platform
