/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "KalediConfig.h"

#include "ThirdParty/rapidjson/document.h"

#include <cstdlib>
#include <cstring>

namespace kaledi {

namespace {

	using ::item::Kind;
	using ::item::NO_FILL;
	using ::item::Style;

	// -- reading one value ------------------------------------------------

	// True when the member is there at all. Absent is not an error anywhere in
	// this file: everything is an overlay onto what the caller already holds.
	const rapidjson::Value* Member(const rapidjson::Value& v, const char* pszName)
	{
		const auto it = v.FindMember(pszName);
		return it != v.MemberEnd() ? &it->value : nullptr;
	}

	bool ReadFloat(const rapidjson::Value& v, const char* pszName, float* pfOut, std::string& ssError)
	{
		const rapidjson::Value* pV = Member(v, pszName);
		if(pV == nullptr)
			return true;
		if(pV->IsNumber() == false) {
			ssError = std::string(pszName) + " must be a number";
			return false;
		}
		*pfOut = static_cast<float>(pV->GetDouble());
		return true;
	}

	bool ReadInt(const rapidjson::Value& v, const char* pszName, int32_t* piOut, std::string& ssError)
	{
		const rapidjson::Value* pV = Member(v, pszName);
		if(pV == nullptr)
			return true;
		if(pV->IsNumber() == false) {
			ssError = std::string(pszName) + " must be a number";
			return false;
		}
		*piOut = static_cast<int32_t>(pV->GetDouble());
		return true;
	}

	// A colour: 0xRRGGBB as a number, "#RRGGBB" or "#AARRGGBB" as a string, or
	// "none" for item::NO_FILL. The string forms are what a web editor's colour
	// picker hands out, and "none" is friendlier to write than 4294967295.
	bool ReadColor(const rapidjson::Value& v, const char* pszName, uint32_t* puOut, std::string& ssError)
	{
		const rapidjson::Value* pV = Member(v, pszName);
		if(pV == nullptr)
			return true;

		if(pV->IsNumber()) {
			*puOut = static_cast<uint32_t>(pV->GetDouble());
			return true;
		}

		if(pV->IsString()) {
			const char* psz = pV->GetString();
			if(std::strcmp(psz, "none") == 0) {
				*puOut = NO_FILL;
				return true;
			}
			if(psz[0] == '#') {
				char*				pszEnd = nullptr;
				const uint32_t u		 = static_cast<uint32_t>(std::strtoul(psz + 1, &pszEnd, 16));
				if(pszEnd != nullptr && *pszEnd == '\0' && pszEnd != psz + 1) {
					*puOut = u;
					return true;
				}
			}
		}

		ssError = std::string(pszName) + " must be a number, \"#RRGGBB\" or \"none\"";
		return false;
	}

	// A font object. Only the size reaches the panel -- see the header.
	bool ReadFont(const rapidjson::Value& v, const char* pszName, ::scale::style::Font* pFont, std::string& ssError)
	{
		const rapidjson::Value* pV = Member(v, pszName);
		if(pV == nullptr)
			return true;
		if(pV->IsObject() == false) {
			ssError = std::string(pszName) + " must be an object";
			return false;
		}

		int32_t iSize = pFont->m_iSize;
		if(ReadInt(*pV, "size", &iSize, ssError) == false)
			return false;
		pFont->m_iSize = static_cast<int>(iSize);

		// Carried for completeness. PainterTvg::CreateFont() ignores both:
		// there is no font engine in this build, only the sizes CMake ran
		// through lv_font_conv.
		if(const rapidjson::Value* pFamily = Member(*pV, "family"); pFamily != nullptr && pFamily->IsString())
			pFont->m_ssFamily = pFamily->GetString();
		if(const rapidjson::Value* pBold = Member(*pV, "bold"); pBold != nullptr && pBold->IsBool())
			pFont->m_bBold = pBold->GetBool();

		return true;
	}

	// -- the band palette -------------------------------------------------

	struct ColorName
	{
		const char*			 pszName;
		::parameter::Color eColor;
	};

	// Common's own parameter::Color, by the name the enum spells. NoColor is
	// in the list because a panel may want the uncoloured stretch of a scale
	// drawn in something other than nothing.
	constexpr ColorName COLOR_NAMES[] = {
		{"NoColor", ::parameter::Color::NoColor},
		{	 "Red",	  ::parameter::Color::Red},
		{ "Yellow",  ::parameter::Color::Yellow},
		{	 "Green",	 ::parameter::Color::Green},
		{	 "Blue",		::parameter::Color::Blue},
		{	 "White",	 ::parameter::Color::White},
	};

	bool ReadColors(const rapidjson::Value& v, ::parameter::gui::Colors* pColors, std::string& ssError)
	{
		const rapidjson::Value* pV = Member(v, "colors");
		if(pV == nullptr)
			return true;
		if(pV->IsObject() == false) {
			ssError = "colors must be an object";
			return false;
		}

		for(const ColorName& cn : COLOR_NAMES) {
			uint32_t uRgb = pColors->GetColor(cn.eColor);
			if(ReadColor(*pV, cn.pszName, &uRgb, ssError) == false)
				return false;
			pColors->SetColor(cn.eColor, uRgb);
		}
		return true;
	}

	// -- the kind ---------------------------------------------------------

	bool ParseKind(const char* pszKind, Kind* peOut)
	{
		struct Entry
		{
			const char* psz;
			Kind			e;
		};
		constexpr Entry KINDS[] = {
			{	 "Arc",	  Kind::Arc},
			{ "BarH",	Kind::BarH},
			{ "BarV",	Kind::BarV},
			{"Value", Kind::Value},
		};

		for(const Entry& e : KINDS) {
			if(std::strcmp(pszKind, e.psz) == 0) {
				*peOut = e.e;
				return true;
			}
		}
		return false;
	}

	// Parse into doc, or say why not.
	bool ParseDocument(const char* pszJson, rapidjson::Document* pDoc, std::string& ssError)
	{
		if(pszJson == nullptr || pszJson[0] == '\0') {
			ssError = "empty";
			return false;
		}
		if(pDoc->Parse(pszJson).HasParseError()) {
			ssError = "not valid JSON";
			return false;
		}
		if(pDoc->IsObject() == false) {
			ssError = "not a JSON object";
			return false;
		}
		return true;
	}

} // namespace

// --------------------------------------------------------------------------

bool ParseItem(const char* pszJson, ItemRequest* pOut, std::string& ssError)
{
	rapidjson::Document doc;
	if(ParseDocument(pszJson, &doc, ssError) == false)
		return false;

	const rapidjson::Value* pKind = Member(doc, "kind");
	if(pKind == nullptr || pKind->IsString() == false) {
		ssError = "kind must be one of Arc, BarH, BarV, Value";
		return false;
	}

	ItemRequest req;
	if(ParseKind(pKind->GetString(), &req.cfg.eKind) == false) {
		ssError = std::string("unknown kind \"") + pKind->GetString() + "\"";
		return false;
	}

	int32_t iId = 0;
	if(ReadInt(doc, "id", &iId, ssError) == false)
		return false;
	if(iId <= 0) {
		ssError = "id must be a can::Id";
		return false;
	}
	req.cfg.eId = static_cast<::can::Id>(iId);

	if(ReadInt(doc, "w", &req.iW, ssError) == false)
		return false;
	if(ReadInt(doc, "h", &req.iH, ssError) == false)
		return false;
	if(req.iW <= 0 || req.iH <= 0) {
		ssError = "w and h must both be positive";
		return false;
	}

	// The item is given the whole pixmap. Where it goes on the panel is the
	// editor's business and nothing this module can help with.
	req.cfg.rc = ::item::Rect{0.0f, 0.0f, static_cast<float>(req.iW), static_cast<float>(req.iH)};

	*pOut = req;
	return true;
}

// --------------------------------------------------------------------------

bool ParseStyle(const char* pszJson, Style* pStyle, std::string& ssError)
{
	rapidjson::Document doc;
	if(ParseDocument(pszJson, &doc, ssError) == false)
		return false;

	const bool bOk =
		ReadFont(doc, "fontTitle", &pStyle->fontTitle, ssError) && ReadFont(doc, "fontValue", &pStyle->fontValue, ssError)
		&& ReadFont(doc, "fontUnit", &pStyle->fontUnit, ssError)
		&& ReadFloat(doc, "fSeparation", &pStyle->fSeparation, ssError)
		&& ReadFloat(doc, "fMargin", &pStyle->fMargin, ssError)
		&& ReadFloat(doc, "fThickness", &pStyle->fThickness, ssError)
		&& ReadFloat(doc, "fPointer", &pStyle->fPointer, ssError)
		&& ReadFloat(doc, "fPointerHalfWidth", &pStyle->fPointerHalfWidth, ssError)
		&& ReadColor(doc, "uPlateRgb", &pStyle->uPlateRgb, ssError)
		&& ReadColor(doc, "uBorderRgb", &pStyle->uBorderRgb, ssError)
		&& ReadColor(doc, "uTrackRgb", &pStyle->uTrackRgb, ssError)
		&& ReadColor(doc, "uTitleRgb", &pStyle->uTitleRgb, ssError) && ReadColors(doc, &pStyle->colors, ssError);

	return bOk;
}

// --------------------------------------------------------------------------

} // namespace kaledi
