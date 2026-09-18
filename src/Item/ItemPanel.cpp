/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "ItemPanel.h"

#include "Platform.h"

#include "Parameter/Param.h"
#include "Parameter/ParamContainer.h"

#include <memory>
#include <utility>

namespace item {

namespace {

	constexpr const char* TAG = "panel";

	// The parameter a row names, or nullptr when the container does not hold it.
	//
	// ParameterContainer::Find() never answers nullptr: an id it does not hold
	// gets a default-constructed dummy filed under can::Id::Invalid. That one
	// has an empty value vector, so an item built on it would read past the end
	// of it on the first frame. The id is what says whether the answer is ours.
	const Param* FindParameter(const ::parameter::ParameterContainer& parameters, ::can::Id eId)
	{
		const Param* pPar = parameters.Find(eId);
		return (pPar != nullptr && pPar->GetId() == eId) ? pPar : nullptr;
	}

} // namespace

// --------------------------------------------------------------------------

std::unique_ptr<Base> MakeItem(const ::parameter::ParameterContainer& parameters, const Style& style, const Config& cfg)
{
	const Param* pPar = FindParameter(parameters, cfg.eId);
	if(pPar == nullptr)
		return nullptr;

	// The only place a kind is branched on. After this a panel holds
	// item::Base and asks it, which is what those two calls are virtual for.
	switch(cfg.eKind) {
	case Kind::Arc:	return std::make_unique<Arc>(style, *pPar, cfg.rc);
	case Kind::BarH:	return std::make_unique<BarH>(style, *pPar, cfg.rc);
	case Kind::BarV:	return std::make_unique<BarV>(style, *pPar, cfg.rc);
	case Kind::Value: return std::make_unique<Value>(style, *pPar, cfg.rc);
	}
	return nullptr;
}

// --------------------------------------------------------------------------

bool Panel::Build(int32_t iW, int32_t iH)
{
	m_bg.emplace(static_cast<uint32_t>(iW), static_cast<uint32_t>(iH), lvgl::ColorFormat::ARGB8888);
	if(m_bg->raw() == nullptr) {
		APP_LOGE(TAG, "no room for a %dx%d ARGB8888 background (%d kB)", iW, iH, iW * iH * 4 / 1024);
		m_bg.reset();
		return false;
	}

	if(m_vItems.empty())
		SetLayout(MockupLayout(static_cast<float>(iW), static_cast<float>(iH)));
	else
		Rebuild();

	APP_LOGI(TAG, "background %dx%d ARGB8888 ready, %d items", iW, iH, static_cast<int>(m_vDrawn.size()));
	return true;
}

// --------------------------------------------------------------------------

void Panel::SetLayout(std::vector<Config> vItems)
{
	m_vItems = std::move(vItems);
	Rebuild();
	Invalidate();
}

// --------------------------------------------------------------------------

void Panel::SetStyle(const Style& style)
{
	// An item keeps its own copy of the style, so changing the panel's means
	// building them all again. That is once per configuration change, not per
	// frame, which is the whole reason they may keep a copy at all.
	m_style = style;
	Rebuild();
	Invalidate();
}

// --------------------------------------------------------------------------

void Panel::Rebuild()
{
	m_vDrawn.clear();
	m_vDrawn.reserve(m_vItems.size());

	for(const Config& cfg : m_vItems) {
		std::unique_ptr<Base> pItem = MakeItem(*m_pParameters, m_style, cfg);
		if(pItem == nullptr) {
			APP_LOGI(TAG, "row for id %u dropped, this unit does not hold it", static_cast<unsigned>(cfg.eId));
			continue;
		}
		m_vDrawn.push_back(std::move(pItem));
	}
}

// --------------------------------------------------------------------------

void Panel::DrawAll(lvgl::Canvas& canvas, bool bStatic)
{
	lv_layer_t layer;
	canvas.init_layer(&layer);
	{
		// The painter has to be gone before finish_layer() waits for the draw
		// units: it owns the descriptor those tasks were queued from.
		Painter P(&layer);
		for(const std::unique_ptr<Base>& pItem : m_vDrawn) {
			if(bStatic)
				pItem->DrawStatic(P);
			else
				pItem->DrawDynamic(P);
		}
		P.Flush();
	}
	canvas.finish_layer(&layer);
}

// --------------------------------------------------------------------------

void Panel::RenderStatic(lvgl::Canvas& canvas, lv_draw_buf_t* pFront, uint32_t uBgRgb)
{
	// LVGL draws into whatever buffer the canvas is pointed at, so pointing it
	// at the background one for the length of this pass is all it takes.
	canvas.set_draw_buf(m_bg->raw());
	canvas.fill_bg(lvgl::Color(uBgRgb), LV_OPA_COVER);

	DrawAll(canvas, true);

	canvas.set_draw_buf(pFront);
}

// --------------------------------------------------------------------------

void Panel::Render(lvgl::Canvas& canvas, lv_draw_buf_t* pFront, uint32_t uBgRgb)
{
	if(m_bg.has_value() == false)
		return;

	if(m_bStaticDone == false) {
		RenderStatic(canvas, pFront, uBgRgb);
		m_bStaticDone = true;
	}

	// The whole point: the plates, the bands and the titles arrive as pixels
	// that cost nothing this frame, and only what moves is rasterised.
	lv_draw_buf_copy(pFront, nullptr, m_bg->raw(), nullptr);

	DrawAll(canvas, false);
}

// --------------------------------------------------------------------------

std::vector<Config> Panel::MockupLayout(float fW, float fH)
{
	// Fractions of the canvas rather than pixels, so the same mockup lands
	// sensibly whatever size the scene gives it.
	auto Box = [fW, fH](float fL, float fT, float fR, float fB) {
		return Rect{fL * fW, fT * fH, (fR - fL) * fW, (fB - fT) * fH};
	};

	return {
		// The arc across the top, between the two upright bars.
		{	 Kind::Arc,				::can::Id::EngineRPM_1, Box(0.26f, 0.00f, 0.74f, 0.46f)},

		// One bar up each side. Rotor on the left, airspeed on the right.
		{ Kind::BarV,				 ::can::Id::RotorRPM_1, Box(0.00f, 0.00f, 0.24f, 0.62f)},
		{ Kind::BarV,		 ::can::Id::IndicatedAirspeed, Box(0.76f, 0.00f, 1.00f, 0.62f)},

		// Two lying on their sides under the arc.
		{ Kind::BarH, ::can::Id::BaroCorrectedAltitude, Box(0.26f, 0.48f, 0.74f, 0.62f)},
		{ Kind::BarH,		 ::can::Id::IndicatedAirspeed, Box(0.26f, 0.64f, 0.74f, 0.78f)},

		// And a pair of bare readouts along the foot.
		{Kind::Value,			  ::can::Id::EngineRPM_1, Box(0.00f, 0.64f, 0.24f, 0.86f)},
		{Kind::Value, ::can::Id::BaroCorrectedAltitude, Box(0.76f, 0.64f, 1.00f, 0.86f)},
	};
}

// --------------------------------------------------------------------------

} // namespace item
