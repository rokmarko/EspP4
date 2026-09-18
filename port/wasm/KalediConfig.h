/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// What the editor says, in JSON, turned into what the items take.
//
// The boundary is JSON rather than a typed object because the schema on the
// far side is a layout editor's, and an editor grows fields. A value_object
// would freeze the shape in C++ and make every new field a rebuild of both
// halves; a JSON object that ignores what it does not know costs one parse per
// render -- microseconds against a ThorVG frame -- and lets the two move
// independently.
//
// RapidJSON, from Common/ThirdParty, which is already on the include path and
// is what src/MqttClient.cpp parses the cloud's messages with. No new
// dependency.
//
// Both parsers are additive: an unknown key is ignored, and a key that is
// present but of the wrong type is an error rather than a silent default. An
// editor that misspells a field should hear about it.

#include "Item/ItemPanel.h"

#include <cstdint>
#include <string>

namespace kaledi {

// One item to draw, and how big a pixmap to draw it on.
//
// The item's own box is the whole pixmap -- {0, 0, w, h}. The editor places
// what comes back; an item fills what it is given.
struct ItemRequest
{
	::item::Config cfg;
	int32_t			iW = 0;
	int32_t			iH = 0;
};

// Parse an item request:
//
//   {"kind": "Arc", "id": 500, "w": 160, "h": 140}
//
// kind is one of Arc, BarH, BarV, Value, and id is a can::Id as a number.
// Returns false with the reason in ssError, leaving pOut alone.
bool ParseItem(const char* pszJson, ItemRequest* pOut, std::string& ssError);

// Overlay any subset of a style onto pStyle:
//
//   {"fThickness": 16, "uPlateRgb": "#0E1424", "fontValue": {"size": 28},
//    "colors": {"Red": "#E00A0A"}}
//
// Every field of item::Style is nameable, spelled as the struct spells it.
// What the JSON does not name keeps whatever pStyle already held, so the
// firmware's own defaults are what an editor starts from and a theme is a
// short object rather than a full one.
//
// A colour is a number (0xRRGGBB) or a "#RRGGBB" string; item::NO_FILL, which
// is how a field asks for nothing to be drawn at all, is the number
// 4294967295, the string "#FFFFFFFF", or the string "none".
//
// A font is an object; only its size has any effect, because
// PainterTvg::CreateFont() has no font engine to ask and snaps to the nearest
// size the build generated.
//
// Returns false with the reason in ssError. pStyle may have been partly
// overlaid by then -- the caller works on a copy for exactly that reason.
bool ParseStyle(const char* pszJson, ::item::Style* pStyle, std::string& ssError);

} // namespace kaledi
