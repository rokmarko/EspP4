/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

#include <cstdint>

// The settings page: a menu of levels on a screen of its own.
//
// Everything the pilot can change lives in Common's own option container --
// units per group, the azimuth reference, the UTC offset -- so this page is a
// view onto `app::Options` and nothing more. It never keeps a copy of a
// setting: a row asks the option what it holds every time it is drawn, and a
// tap hands the next value straight back. What is dirty afterwards is what
// `app::Settings::Save()` writes.
//
// The page is built once, on its own `lvgl::Screen`, and the levels are a
// static table. Changing level rebuilds the rows -- there are never more than
// a handful -- and repaints the header; nothing else is touched, and no timer
// runs here at all. That is the whole reason it is a second screen rather than
// a sixth scene: the instrument canvas costs up to 60 ms a frame on the panel,
// and a settings page has no business paying that.
//
// The title bar is an eye: the panel's own circle for the upper lid, a shallow
// arc bulging the other way for the lower one, the two meeting in a point at
// each side. On a round screen a rectangular title bar wastes the corners it
// does not have; this one is cut to the glass, and each level tints it.

namespace menu {

// What a tap on the title bar does once there is no level left to go up to.
using CloseHandler = void (*)();

// The keys the page navigates by. Not LVGL's own codes: the debug console
// sends these too, and it has no business including lvgl.h to say "down".
enum class Key : uint8_t
{
	Up,
	Down,
	Left,
	Right,
	Enter,
	Esc
};

// Build the page. Must be called with the LVGL lock held, and with the model
// already running -- the rows read their values out of `app::Options`.
//
// The page is built hidden: it does not become the active screen until Show().
//
// Returns false if the header canvas could not be allocated.
bool CreatePage();

// Make the page the active screen, at the root level, and take the keys.
void Show();

// Put it away: release the keys and write out whatever the pilot changed.
//
// Called by whoever loads another screen, not by the page -- it never loads
// anybody else's. Saving belongs here rather than in the row that changed it:
// one flash write on the way out, instead of one inside every tap.
void Hide();

// Hand back the handler called when the pilot leaves the root level. The
// caller owns what happens next -- this page never loads anybody else's
// screen.
void SetCloseHandler(CloseHandler pfnClose);

// Navigate by key.
//
// Up and left step the selection back, down and right step it on, Enter
// activates what it rests on and Esc goes up a level -- out of the page, at
// the root. Every key is ignored while the page is not the active screen.
//
// The page carries that selection itself rather than leaving it to an LVGL
// group, because a finger never moves it: it rests on the title bar, where it
// marks nothing at all, until a key steps it. While the page is up it points
// every keypad and encoder input device at itself, so the simulator's SDL
// keyboard drives it as it stands, and a keypad or a rotary knob would drive
// it on the board. The debug console feeds it the same codes.
//
// Takes the LVGL lock itself.
void HandleKey(Key eKey);

// The level the page is showing, for the console's stats line.
const char* LevelName();

// Which row is selected, or -1 for the title bar.
int Selection();

} // namespace menu
