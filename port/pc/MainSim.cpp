/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The simulator's entry point.
//
// Where MainEsp.cpp starts a MIPI-DSI panel through the Waveshare BSP, this
// opens an SDL window the same size as that panel -- 720x720 -- and hands over
// to app::Startup(). Everything above the display is the firmware, unchanged:
// the same scenes, the same ThorVG rasteriser, the same flight model, the same
// CANaerospace stack, the same one-character console.
//
// Three things differ from the board and are worth knowing:
//
//  - **LVGL runs on this thread.** The board has esp_lvgl_adapter's own task;
//    here main() is the loop, and everything else -- the model, the console --
//    takes platform::LockDisplay() the way it takes the BSP's lock there.
//  - **Closing the window ends the process.** LV_SDL_DIRECT_EXIT in lv_conf.h
//    makes SDL_QUIT call exit(), which is the only tidy way out while the
//    model and console threads are still running.
//  - **The CAN adapter is optional.** Without one the port falls back to
//    self-test and the instruments run off Model::Simulate(), which is what
//    makes the simulator show something on a desk.

#include "KanardiaCommon.h"

#include "App.h"
#include "Platform.h"

#include "lvgl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char* TAG = "main";

// The panel this stands in for. The scenes place their widgets against it, so
// it is not a preference.
constexpr int32_t PANEL_W = 720;
constexpr int32_t PANEL_H = 720;

void Usage(const char* pszSelf)
{
	std::fprintf(
		stderr,
		"usage: %s [--can DEVICE] [--state DIR]\n"
		"\n"
		"  --can DEVICE   serial device of the Kanardia CANU adapter, e.g.\n"
		"                 /dev/ttyUSB0. Without one the CAN port runs in\n"
		"                 self-test and the instruments are driven by the\n"
		"                 model's own simulated traffic.\n"
		"  --state DIR    where the option and parameter blobs are kept.\n"
		"                 Defaults to $XDG_STATE_HOME/espp4-sim.\n"
		"\n"
		"Both are also read from the environment, as ESPP4_CAN_DEVICE and\n"
		"ESPP4_SIM_STATE; the switches simply set those.\n"
		"\n"
		"The one-character debug console is on stdin: h for help, i for\n"
		"stats, t to change scene, s or S for a screenshot. The settings\n"
		"page takes the arrows, Enter and Esc, there and in the window.\n",
		pszSelf
	);
}

// Returns false if the command line does not make sense, in which case the
// caller should print the usage and stop.
bool ParseArgs(int argc, char** argv)
{
	for(int i = 1; i < argc; ++i) {
		const char* pszArg = argv[i];
		const bool	bLast	 = (i + 1) >= argc;

		if(std::strcmp(pszArg, "--can") == 0 && bLast == false)
			::setenv("ESPP4_CAN_DEVICE", argv[++i], 1);
		else if(std::strcmp(pszArg, "--state") == 0 && bLast == false)
			::setenv("ESPP4_SIM_STATE", argv[++i], 1);
		else
			return false;
	}
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	if(ParseArgs(argc, argv) == false) {
		Usage(argv[0]);
		return 2;
	}

	// LVGL's own clock and delay come from SDL: lv_sdl_window_create() installs
	// SDL_GetTicks and SDL_Delay itself, so there is nothing to set here.
	lv_init();

	lv_display_t* pDisplay = lv_sdl_window_create(PANEL_W, PANEL_H);
	if(pDisplay == nullptr) {
		APP_LOGE(TAG, "could not open a %dx%d SDL window", static_cast<int>(PANEL_W), static_cast<int>(PANEL_H));
		return 1;
	}
	lv_sdl_window_set_title(pDisplay, "Kanardia EspP4 simulator");

	// The pointer stands in for the panel's GT911: a click is a tap, which is
	// what changes scene.
	lv_sdl_mouse_create();
	lv_sdl_mousewheel_create();
	lv_sdl_keyboard_create();

	APP_LOGI(TAG, "panel %dx%d, SDL window", static_cast<int>(PANEL_W), static_cast<int>(PANEL_H));

	if(app::Startup() == false)
		return 1;

	// LVGL's own loop. lv_timer_handler() says how long it is happy to wait;
	// the cap keeps the pointer responsive when it says "a long time".
	for(;;) {
		uint32_t uIdleMs = 0;
		{
			platform::DisplayLock lock;
			uIdleMs = lv_timer_handler();
		}
		platform::SleepMs(uIdleMs > 16 ? 16 : (uIdleMs == 0 ? 1 : uIdleMs));
	}

	return 0;
}
