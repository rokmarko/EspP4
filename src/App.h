/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// Bringing the application up, once there is a display to put it on.
//
// Each port's main() does the one thing only it can -- start the MIPI-DSI
// panel through the Waveshare BSP, or open an SDL window -- and then calls
// this. Everything after that point is the same on both builds, and the order
// it happens in is load-bearing, so it lives in one place.

namespace app {

// Formatter, console, model loop, scene. In that order, for reasons spelled
// out where it happens.
//
// Must be called with an LVGL display already created and no LVGL lock held.
//
// Returns false if the scene could not be built, which is fatal -- there is
//         nothing to look at. A console or CAN port that fails to start is
//         logged and survived.
bool Startup();

} // namespace app
