/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "KalediRenderer.h"

#include "KalediConfig.h"

#include "Platform.h"

#include "Avio/Format/AvioFormat.h"
#include "FBS/ParamStorageItem_generated.h"
#include "Parameter/Param.h"
#include "Parameter/ParamLoaderBase.h"
#include "Unit/UnitFormatterUtf8.h"
#include "Unit/UnitKeys.h"

#include "ThirdParty/rapidjson/stringbuffer.h"
#include "ThirdParty/rapidjson/writer.h"

#include <memory>
#include <utility>

namespace kaledi {

namespace {

	constexpr const char* TAG = "kaledi";

	// Hand Common's formatting layer the formatter it works through.
	//
	// The same two lines as src/App.cpp's InstallUnitFormatter(), and for the
	// same reason: avio::format keeps one process-wide unit::Formatter*,
	// asserts on it, and every number and unit glyph an item letters reaches
	// it from there. It has to be installed before anything formats, which
	// here means before the first Render().
	void InstallUnitFormatter()
	{
		static const unit::FormatterUtf8 formatter;
		avio::format::SetUnitFormatter(&formatter);
	}

	// ParameterLoaderBase::FindDefault() is protected, and it is the only way
	// to ask whether Common knows an id before CreateCallback() asserts on it.
	// A derived type is what C++ offers for that.
	struct Loader : public ::parameter::ParameterLoaderBase
	{
		using ::parameter::ParameterLoaderBase::FindDefault;
	};

	// One channel of a premultiplied pixel, divided back out. Rounded rather
	// than truncated, and clamped, because a blend that lands a channel one
	// above its own alpha would otherwise come back as something over 255.
	uint8_t Unpremultiply(uint32_t uChannel, uint32_t uAlpha)
	{
		const uint32_t u = (uChannel * 255u + uAlpha / 2u) / uAlpha;
		return static_cast<uint8_t>(u < 255u ? u : 255u);
	}

	// The parameter a container holds under this id, or nullptr.
	//
	// ParameterContainer::Find() never answers nullptr -- an id it does not
	// hold gets a default-constructed dummy filed under can::Id::Invalid -- so
	// the id is what says whether the answer is ours. Same guard as
	// item::MakeItem()'s.
	::parameter::Parameter* FindHeld(::parameter::ParameterContainer& pc, ::can::Id eId)
	{
		::parameter::Parameter* pP = pc.Find(eId);
		return (pP != nullptr && pP->GetId() == eId) ? pP : nullptr;
	}

} // namespace

// --------------------------------------------------------------------------

Renderer::Renderer()
{
	InstallUnitFormatter();
	wasm::LvglStart();

	APP_LOGI(TAG, "renderer ready");
}

// --------------------------------------------------------------------------

int Renderer::SetParameter(const std::string& sBlob)
{
	m_ssError.clear();

	if(sBlob.empty()) {
		m_ssError = "empty parameter blob";
		return 0;
	}

	// The board trusts these bytes because CanProcessor checked the sender's
	// CRC-16 before handing them over. Here they come from JavaScript, and
	// ParamStorage::ApplyTo()'s own checks are ASSERTs -- no-ops in a release
	// build -- so the flatbuffer has to be verified before it is walked.
	::flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(sBlob.data()), sBlob.size());
	if(::parameter::fbs::VerifyParamItemBuffer(verifier) == false) {
		m_ssError = "not a valid ParamItem flatbuffer";
		return 0;
	}

	const ::parameter::fbs::ParamItem* pItem = ::parameter::fbs::GetParamItem(sBlob.data());
	if(pItem == nullptr) {
		m_ssError = "not a ParamItem";
		return 0;
	}

	// ApplyTo() dereferences all three names unconditionally.
	if(pItem->full_name() == nullptr || pItem->short_name() == nullptr || pItem->tiny_name() == nullptr) {
		m_ssError = "ParamItem carries no names";
		return 0;
	}

	const ::can::Id			eId = static_cast<::can::Id>(pItem->can_id());
	::parameter::Parameter* pP	 = FindHeld(m_pc, eId);

	if(pP == nullptr) {
		// Not held yet. Common's default table is what says which function an
		// id has, and therefore its system unit, its filter and its limits --
		// none of which the blob carries. Without an entry there we would have
		// to invent them, so an unknown id is refused rather than guessed at.
		if(Loader::FindDefault(eId) == nullptr) {
			m_ssError = "can::Id " + std::to_string(pItem->can_id()) + " is not a parameter Common knows";
			return 0;
		}

		Loader::CreateCallback(m_pc, nullptr, m_units, eId, [this](::can::Id eIdRead, uint8_t uIndex) {
			return m_nod.GetFloat(eIdRead, uIndex);
		});

		pP = FindHeld(m_pc, eId);
		if(pP == nullptr) {
			m_ssError = "could not create a parameter for can::Id " + std::to_string(pItem->can_id());
			return 0;
		}
	}

	::parameter::ParamStorage::ApplyTo(pP, pItem);

	APP_LOGI(TAG, "parameter %u set, %d bands", static_cast<unsigned>(pItem->can_id()), pP->GetBands().GetCount());
	return static_cast<int>(pItem->can_id());
}

// --------------------------------------------------------------------------

int Renderer::SetParameters(const std::string& sBlob)
{
	m_ssError.clear();

	if(sBlob.empty()) {
		m_ssError = "empty parameter blob";
		return 0;
	}

	// Load() decompresses into m_storage's own buffer and keeps a flatbuffer
	// pointer into it, which is why m_storage is a member and not a local. It
	// answers silently on a bad CRC or bad LZO, leaving that pointer null --
	// there is no return value to check, and the count below is the only
	// symptom a broken blob has.
	m_storage.Load(std::vector<uint8_t>(sBlob.begin(), sBlob.end()));

	const int iFromBlob = RebuildParameters();
	if(iFromBlob == 0)
		m_ssError = "blob supplied no parameters; it may be corrupt";

	APP_LOGI(TAG, "%d of %d parameters came from the blob", iFromBlob, CountHeld());
	return iFromBlob;
}

// --------------------------------------------------------------------------

int Renderer::LoadDefaults()
{
	m_ssError.clear();

	// An empty blob is how ParamStorage is told to forget the one it holds:
	// Load() clears its flatbuffer pointer and returns, and every Lookup()
	// after that answers nothing. CreateNODs() then builds the table with no
	// blob laid over it, which is the defaults.
	m_storage.Load({});
	RebuildParameters();

	const int iCount = CountHeld();
	APP_LOGI(TAG, "%d parameters at their defaults", iCount);
	return iCount;
}

// --------------------------------------------------------------------------

int Renderer::CountHeld() const
{
	int iCount						 = 0;
	const auto [itBegin, itEnd] = m_pc.GetIterators();
	for(auto it = itBegin; it != itEnd; ++it) {
		if(it->second.GetId() != ::can::Id::Invalid)
			iCount++;
	}
	return iCount;
}

// --------------------------------------------------------------------------

int Renderer::RebuildParameters()
{
	// CreateNODs() walks Common's default table, lays whatever m_storage holds
	// over each entry and wires every parameter's callback to the NOD. It is
	// insert_or_assign underneath, so calling it again replaces the set rather
	// than accumulating two of them -- and because the map is node-based, a
	// Parameter that was already there keeps its address.
	Loader::CreateNODs(m_pc, m_storage, m_units, m_nod);

	// How many of them the blob actually supplied. GetCount() answers zero for
	// an id the storage does not hold and a positive count for one it does,
	// because every ParamItem ParamStorage writes carries the parameter's own
	// value count, which is never zero.
	int iFromBlob					 = 0;
	const auto [itBegin, itEnd] = m_pc.GetIterators();
	for(auto it = itBegin; it != itEnd; ++it) {
		if(it->second.GetId() != ::can::Id::Invalid && m_storage.GetCount(it->second.GetId()) > 0)
			iFromBlob++;
	}
	return iFromBlob;
}

// --------------------------------------------------------------------------

bool Renderer::SetValue(int iCanId, float fValue)
{
	m_ssError.clear();

	const ::can::Id			eId = static_cast<::can::Id>(iCanId);
	::parameter::Parameter* pP	 = FindHeld(m_pc, eId);
	if(pP == nullptr) {
		m_ssError = "no parameter for can::Id " + std::to_string(iCanId);
		return false;
	}

	m_nod.SetFloat(eId, 0, fValue);

	// Straight to the value, not towards it. Parameter::GetValueSystem() runs
	// a low-pass with the function's own time constant -- 400 ms for an engine
	// rpm -- and re-samples at most every 30 ms, which on a panel is what
	// keeps a needle from twitching and in an editor would mean the operator
	// types 2450 and watches the widget creep up to it. ForceValue() is
	// Common's own escape hatch for exactly this.
	for(uint32_t u = 0; u < pP->GetCount(); u++)
		pP->ForceValue(static_cast<int>(u));

	return true;
}

// --------------------------------------------------------------------------

std::string Renderer::GetParameters() const
{
	rapidjson::StringBuffer							 buf;
	rapidjson::Writer<rapidjson::StringBuffer> w(buf);

	w.StartArray();
	const auto [itBegin, itEnd] = m_pc.GetIterators();
	for(auto it = itBegin; it != itEnd; ++it) {
		const ::parameter::Parameter& par = it->second;
		if(par.GetId() == ::can::Id::Invalid)
			continue;

		w.StartObject();
		w.Key("id");
		w.Int(static_cast<int>(par.GetId()));
		w.Key("name");
		w.String(par.GetName(::parameter::NameLength::Long).c_str());
		w.Key("short");
		w.String(par.GetName(::parameter::NameLength::Short).c_str());
		w.Key("unit");
		w.String(unit::GetSignature(par.GetUnitKeyUser()));
		w.Key("bands");
		w.Int(par.GetBands().GetCount());
		// The band range, in the system unit -- which is the unit SetValue()
		// takes, so an editor can scale a slider straight off these two.
		w.Key("low");
		w.Double(par.GetBands().GetLow());
		w.Key("high");
		w.Double(par.GetBands().GetHigh());
		w.EndObject();
	}
	w.EndArray();

	return buf.GetString();
}

// --------------------------------------------------------------------------

bool Renderer::SetStyle(const std::string& sJson)
{
	m_ssError.clear();

	// Onto a copy, so that a JSON object which is good for three fields and
	// bad for the fourth leaves the style it had rather than half of a new one.
	::item::Style style = m_style;
	if(ParseStyle(sJson.c_str(), &style, m_ssError) == false)
		return false;

	m_style = style;
	return true;
}

// --------------------------------------------------------------------------

emscripten::val Renderer::Render(const std::string& sJson)
{
	m_ssError.clear();

	ItemRequest req;
	if(ParseItem(sJson.c_str(), &req, m_ssError) == false)
		return emscripten::val::null();

	std::unique_ptr<::item::Base> pItem = ::item::MakeItem(m_pc, m_style, req.cfg);
	if(pItem == nullptr) {
		m_ssError = "no parameter for can::Id " + std::to_string(static_cast<int>(req.cfg.eId));
		return emscripten::val::null();
	}

	if(m_surface.Resize(req.iW, req.iH) == false) {
		m_ssError = "no room for a pixmap that size";
		return emscripten::val::null();
	}

	lvgl::Canvas& canvas = m_surface.GetCanvas();

	// Transparent, not the panel's background: the editor composes these over
	// whatever it is showing behind them, and a widget that arrived with its
	// own dark rectangle could not be placed on anything.
	canvas.fill_bg(lvgl::Color(0x000000), LV_OPA_TRANSP);

	{
		lv_layer_t layer;
		canvas.init_layer(&layer);
		{
			// The painter has to be gone before finish_layer() waits for the
			// draw units: it owns the descriptor those tasks were queued from.
			::item::Painter P(&layer);

			// Both halves, in the order a panel draws them. The static/dynamic
			// split exists to keep a sheet of a dozen items under a 33 ms
			// timer on the board; one item on a desktop is not that problem,
			// and rendering both together is what makes a pixmap complete.
			pItem->DrawStatic(P);
			pItem->DrawDynamic(P);
			P.Flush();
		}
		canvas.finish_layer(&layer);
	}

	// Out of LVGL's buffer and into the browser's. Two things change on the
	// way, and getting either wrong is invisible until it is not:
	//
	//  - **the order.** LVGL's ARGB8888 is b, g, r, a in memory -- see
	//    ToColor32() in src/PainterTvg.h -- and ImageData wants r, g, b, a, so
	//    the two ends of each pixel swap.
	//  - **the alpha.** What LVGL leaves behind is premultiplied: draw white at
	//    alpha 120 onto a transparent canvas and the pixel reads
	//    (120, 120, 120, 120). ImageData is straight alpha, so it has to be
	//    divided back out. Skipping this costs nothing on the flat interior of
	//    a plate, where alpha is 254, and darkens every antialiased edge and
	//    every partly transparent band -- which, on a widget whose whole job is
	//    to be composed over the editor's own background, is the difference
	//    between a clean edge and a grubby one.
	//
	// The stride is read rather than assumed: LV_DRAW_BUF_STRIDE_ALIGN is 1 in
	// this build, but a buffer is entitled to pad its rows.
	const lv_draw_buf_t* pBuf	  = m_surface.GetDrawBuf();
	const uint32_t			uStride = pBuf->header.stride;
	const size_t			uBytes  = static_cast<size_t>(req.iW) * static_cast<size_t>(req.iH) * 4u;

	m_vRgba.resize(uBytes);
	for(int32_t y = 0; y < req.iH; y++) {
		const uint8_t* pSrc = pBuf->data + static_cast<size_t>(y) * uStride;
		uint8_t*			pDst = m_vRgba.data() + static_cast<size_t>(y) * static_cast<size_t>(req.iW) * 4u;
		for(int32_t x = 0; x < req.iW; x++) {
			const uint32_t uA = pSrc[3];
			if(uA == 0) {
				// Nothing was drawn here, and the colour under a zero alpha is
				// not a colour -- dividing it out would only invent one.
				pDst[0] = 0;
				pDst[1] = 0;
				pDst[2] = 0;
				pDst[3] = 0;
			}
			else {
				pDst[0] = Unpremultiply(pSrc[2], uA);
				pDst[1] = Unpremultiply(pSrc[1], uA);
				pDst[2] = Unpremultiply(pSrc[0], uA);
				pDst[3] = static_cast<uint8_t>(uA);
			}
			pSrc += 4;
			pDst += 4;
		}
	}

	// A copy, not a view into the wasm heap: ALLOW_MEMORY_GROWTH can detach
	// every existing typed array the moment anything allocates, and an editor
	// holding one of these across a call would find it empty.
	return emscripten::val(emscripten::typed_memory_view(m_vRgba.size(), m_vRgba.data())).call<emscripten::val>("slice");
}

// --------------------------------------------------------------------------

} // namespace kaledi
