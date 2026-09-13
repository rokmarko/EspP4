/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The settings page. See MenuPage.h for what it is and why it is a screen of
// its own; this file is the level table, the rows built from it, and the eye.

#include "MenuPage.h"

#include "Platform.h"

#include "lvgl_cpp.h"

#include "AppModel.h"
#include "AppOptions.h"
#include "KanardiaFont.h"
#include "StorageOptions.h"

#include "Avio/AvioDefs.h"
#include "Avio/Format/AvioFormat.h"
#include "Option/OptionsModel.h"
#include "Parameter/ParamUnitGroup.h"

#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using lvgl::Align;
using lvgl::Canvas;
using lvgl::Color;
using lvgl::DrawBuf;
using lvgl::FlexAlign;
using lvgl::FlexFlow;
using lvgl::Label;
using lvgl::ObjFlag;
using lvgl::Opacity;
using lvgl::Part;
using lvgl::Screen;
using lvgl::State;
using lvgl::VectorDraw;
using lvgl::VectorPath;

constexpr const char* TAG = "menu";

// lvgl_cpp wraps every widget except a bare container, and the page is mostly
// bare containers. Widget<> is the binding's own base for exactly this, so a
// name and the inherited constructors are the whole of it.
class Panel : public lvgl::Widget<Panel>
{
public:
	using Widget::Widget;
};

// -----------------------------------------------------------------------------
//  Geometry
// -----------------------------------------------------------------------------

// The panel is 720x720 and round, on both builds. The centre and the radius
// are read off the display so the page lands in the middle of whatever it is
// actually given, but every number below was chosen against a 360 px radius:
// the eye's corners, the row width and the list height are all cut to that
// glass, and a smaller panel would need them re-cut, not scaled.
constexpr int32_t PANEL_R = 360;

// The eye. Its corners sit on the glass at LENS_CORNER_Y, the upper lid is the
// panel's own circle inset by LENS_RIM, and the lower lid is a much shallower
// arc bulging the other way down to LENS_BELLY_Y. The two meet in a point at
// each side, which is what makes it an eye rather than a band.
constexpr float LENS_RIM		= 7.0f;
constexpr float LENS_CORNER_Y = 150.0f;
constexpr float LENS_BELLY_Y	= 210.0f;
// How far inside the outer lens the darker one that carries the title sits.
constexpr float LENS_INNER = 13.0f;

// The header canvas covers the eye and nothing else. ARGB8888 because
// lv_draw_sw_vector renders straight into that and would otherwise allocate a
// full-area temporary and blend it back -- the same reason the instrument
// canvas is ARGB8888. It is painted on a change of level and not again.
constexpr int32_t HEADER_H = 216;

// The list of rows, in the widest band the glass leaves under the eye. A row
// is a rounded box the full width; five of them fit, and anything past that
// scrolls.
constexpr int32_t LIST_TOP = 232;
constexpr int32_t LIST_H	= 362;
constexpr int32_t LIST_W	= 500;
constexpr int32_t ROW_H		= 62;
constexpr int32_t ROW_GAP	= 8;

constexpr uint32_t BG_COLOR = 0x080B14;
// The ground a row sits on, and the text on it.
constexpr uint32_t ROW_BG		= 0x121A2E;
constexpr uint32_t ROW_LABEL	= 0xE8F4FF;
constexpr uint32_t ROW_DIMMED = 0x6F86B5;

// -----------------------------------------------------------------------------
//  Colour helpers
// -----------------------------------------------------------------------------

// lv_color32_t is laid out blue, green, red, alpha -- the same order
// VectorScene builds its colours in.
constexpr lv_color32_t Rgba(uint32_t rgb, uint8_t a = 0xFF)
{
	return lv_color32_t{
		static_cast<uint8_t>(rgb & 0xFFu),
		static_cast<uint8_t>((rgb >> 8) & 0xFFu),
		static_cast<uint8_t>((rgb >> 16) & 0xFFu),
		a,
	};
}

// One accent, darkened or lightened per channel. Every shade on the page is
// the level's own colour through this, so a new level needs one number.
constexpr uint32_t Shade(uint32_t rgb, float fFactor)
{
	uint32_t uOut = 0;
	for(int i = 16; i >= 0; i -= 8) {
		const float f = static_cast<float>((rgb >> i) & 0xFFu) * fFactor;
		const auto	c = static_cast<uint32_t>(f > 255.0f ? 255.0f : f);
		uOut			  = (uOut << 8) | c;
	}
	return uOut;
}

lv_grad_stop_t GradStop(uint32_t rgb, uint8_t frac)
{
	lv_grad_stop_t s{};
	s.color = lv_color_hex(rgb);
	s.opa	  = LV_OPA_COVER;
	s.frac  = frac;
	return s;
}

// -----------------------------------------------------------------------------
//  What the rows read and write
// -----------------------------------------------------------------------------

// The one option container, or nullptr before the model loop has started.
// Every accessor below goes through this and answers something harmless when
// it is not there, because the page can be built before the bus has said
// anything -- but not before the model exists.
app::Options* GetOptions()
{
	app::Model* pModel = app::GetModel();
	return pModel != nullptr ? &pModel->GetOptions() : nullptr;
}

using parameter::UnitGroup;
namespace ug = parameter::unit_group_util;

// The unit a group is currently shown in, as plain ASCII.
//
// The signature, not the glyph: a settings row has room to spell "ft/min" out
// and a pilot picking a unit should read it, not decode it. The instruments
// are where the private-use glyphs earn their keep.
std::string UnitValue(int iArg)
{
	const app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return "--";

	const unit::Key eKey = pOptions->m_units.GetKey(static_cast<UnitGroup>(iArg));
	return avio::format::ToString(eKey, avio::format::UnitExtension::Signature);
}

// Step to the next unit Common allows for that group, wrapping.
void UnitNext(int iArg)
{
	app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return;

	const auto						eGroup = static_cast<UnitGroup>(iArg);
	const std::span<unit::Key> sKeys	 = ug::GetUnits(eGroup);
	if(sKeys.empty())
		return;

	const unit::Key eNow = pOptions->m_units.GetKey(eGroup);

	size_t uAt = 0;
	while(uAt < sKeys.size() && sKeys[uAt] != eNow)
		uAt++;
	// A key the group does not list -- start the cycle from its first.
	const size_t uNext = uAt < sKeys.size() ? (uAt + 1) % sKeys.size() : 0;

	pOptions->m_units.SetKey(eGroup, sKeys[uNext]);
}

// --- Azimuth --------------------------------------------------------------

std::string AzimuthRefValue(int)
{
	const app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return "--";
	return pOptions->m_azimuth.GetReference() == avio::AzimuthRef::True ? "True" : "Magnetic";
}

void AzimuthRefNext(int)
{
	app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return;

	const avio::AzimuthRef eNow = pOptions->m_azimuth.GetReference();
	pOptions->m_azimuth.SetReference(
		eNow == avio::AzimuthRef::True ? avio::AzimuthRef::Magnetic : avio::AzimuthRef::True
	);
}

std::string AzimuthModeValue(int)
{
	const app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return "--";
	return pOptions->m_azimuth.GetMode() == avio::AzimuthMode::Track ? "Track" : "Heading";
}

void AzimuthModeNext(int)
{
	app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return;

	const avio::AzimuthMode eNow = pOptions->m_azimuth.GetMode();
	pOptions->m_azimuth.SetMode(
		eNow == avio::AzimuthMode::Track ? avio::AzimuthMode::Heading : avio::AzimuthMode::Track
	);
}

// --- Time -----------------------------------------------------------------

// Common keeps the local offset in seconds; nobody flies a zone finer than the
// half hour, so that is the step and the whole usable range is 54 of them.
constexpr int32_t UTC_STEP_SEC = 30 * 60;
constexpr int32_t UTC_MIN_SEC	 = -12 * 3600;
constexpr int32_t UTC_MAX_SEC	 = 14 * 3600;

std::string UtcOffsetValue(int)
{
	const app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return "--";

	const int32_t iSec = pOptions->m_units.GetLocalTimeDifference();
	const int32_t iAbs = iSec < 0 ? -iSec : iSec;

	// int32_t is long for this toolchain, which %d refuses under -Werror=format
	// -- the same mismatch cmake/KanardiaSources.cmake has to switch off for
	// Common's own sources. Cast rather than reach for PRId32: these are hours
	// and minutes, and int is what the format says.
	char sz[16];
	std::snprintf(
		sz,
		sizeof(sz),
		"%c%02d:%02d",
		iSec < 0 ? '-' : '+',
		static_cast<int>(iAbs / 3600),
		static_cast<int>((iAbs % 3600) / 60)
	);
	return sz;
}

void UtcOffsetNext(int)
{
	app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return;

	int32_t iSec = pOptions->m_units.GetLocalTimeDifference() + UTC_STEP_SEC;
	if(iSec > UTC_MAX_SEC)
		iSec = UTC_MIN_SEC;
	pOptions->m_units.SetLocalTimeDifferenceMin(iSec / 60);
}

std::string TotalizerValue(int)
{
	const app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return "--";
	return pOptions->m_units.GetTotalizerFormat() == avio::TotalizerFormat::HoursMinutes ? "h:mm" : "decimal h";
}

void TotalizerNext(int)
{
	app::Options* pOptions = GetOptions();
	if(pOptions == nullptr)
		return;

	const avio::TotalizerFormat eNow = pOptions->m_units.GetTotalizerFormat();
	pOptions->m_units.SetTotalizerFormat(
		eNow == avio::TotalizerFormat::HoursMinutes ? avio::TotalizerFormat::DecimalHours
																  : avio::TotalizerFormat::HoursMinutes
	);
}

// --- System ---------------------------------------------------------------

std::string BuildValue(int)
{
	return platform::Name();
}

std::string StoredValue(int)
{
	char sz[16];
	std::snprintf(sz, sizeof(sz), "%u", static_cast<unsigned>(app::OptionsLoaded()));
	return sz;
}

std::string PushCountValue(int)
{
	char sz[16];
	std::snprintf(sz, sizeof(sz), "%u", static_cast<unsigned>(app::ParameterPushCount()));
	return sz;
}

// Force the whole set out, not just what is dirty. This is the row a pilot
// reaches for when they want to be sure, and "wrote nothing because nothing
// changed" is not what that row should mean.
void SaveNow(int)
{
	app::Model*		pModel	= app::GetModel();
	app::Settings& settings = app::GetSettings();
	if(pModel == nullptr || settings.IsOpen() == false) {
		APP_LOGE(TAG, "no settings store to save to");
		return;
	}

	const uint32_t uWritten = settings.Save(pModel->GetOptions(), false);
	APP_LOGI(TAG, "saved %u option blobs", static_cast<unsigned>(uWritten));
}

// The console's `P` command, on a row. Self-test only, which is the usual
// state on a desk and never the state in an aircraft, so on a live bus this
// does nothing and says so.
void PushParameter(int)
{
	app::Model* pModel = app::GetModel();
	if(pModel == nullptr)
		return;
	APP_LOGI(TAG, "parameter push %s", pModel->SimulateParameterPush() ? "sent" : "refused (not in self-test)");
}

// -----------------------------------------------------------------------------
//  The levels
// -----------------------------------------------------------------------------

enum class Kind : uint8_t
{
	// Opens the level named in iArg.
	Submenu,
	// A tap steps the option to its next value; the value is shown on the right.
	Choice,
	// A tap runs pfnAction. Nothing is shown on the right.
	Action,
	// Read-only. Not clickable, and drawn dimmer to say so.
	Info,
};

using ValueFn	= std::string (*)(int iArg);
using ActionFn = void (*)(int iArg);

struct Item
{
	Kind			eKind;
	const char* pcLabel;
	int			iArg		 = 0;
	ValueFn		pfnValue	 = nullptr;
	ActionFn		pfnAction = nullptr;
};

enum Level : int
{
	LevelRoot,
	LevelUnits,
	LevelAzimuth,
	LevelTime,
	LevelSystem,
	LevelCount
};

// Every level's rows. The unit rows carry their group in iArg and share one
// pair of functions, which is what keeps this table as short as it is: Common
// already knows which units a group allows and what it is called.
constexpr Item ROOT_ITEMS[] = {
	{Kind::Submenu,	 "Units",	 LevelUnits},
	{Kind::Submenu, "Azimuth", LevelAzimuth},
	{Kind::Submenu,	 "Time",		LevelTime},
	{Kind::Submenu,	 "System",  LevelSystem},
};

constexpr Item UNIT_ITEMS[] = {
	{Kind::Choice,				"Speed",				static_cast<int>(UnitGroup::Speed), UnitValue, UnitNext},
	{Kind::Choice,			"Altitude",			static_cast<int>(UnitGroup::Altitude), UnitValue, UnitNext},
	{Kind::Choice,	"Vertical speed",	static_cast<int>(UnitGroup::VerticalSpeed), UnitValue, UnitNext},
	{Kind::Choice, "Baro correction", static_cast<int>(UnitGroup::BaroCorrection), UnitValue, UnitNext},
	{Kind::Choice,			"Distance",			static_cast<int>(UnitGroup::Distance), UnitValue, UnitNext},
	{Kind::Choice,		"Temperature",		static_cast<int>(UnitGroup::Temperature), UnitValue, UnitNext},
	{Kind::Choice,			"Pressure",			static_cast<int>(UnitGroup::Pressure), UnitValue, UnitNext},
	{Kind::Choice,			"Fuel flow",				 static_cast<int>(UnitGroup::Flow), UnitValue, UnitNext},
	{Kind::Choice,		"Engine RPM",		  static_cast<int>(UnitGroup::EngineRPM), UnitValue, UnitNext},
};

constexpr Item AZIMUTH_ITEMS[] = {
	{Kind::Choice, "Reference", 0,	 AzimuthRefValue,	AzimuthRefNext},
	{Kind::Choice,		"Mode", 0, AzimuthModeValue, AzimuthModeNext},
};

constexpr Item TIME_ITEMS[] = {
	{Kind::Choice, "UTC offset", 0, UtcOffsetValue, UtcOffsetNext},
	{Kind::Choice,	"Totalizer", 0, TotalizerValue, TotalizerNext},
};

constexpr Item SYSTEM_ITEMS[] = {
	{Kind::Action, "Save settings now", 0, nullptr, SaveNow},
	{Kind::Action, "Push test parameter", 0, nullptr, PushParameter},
	{Kind::Info, "Build", 0, BuildValue},
	{Kind::Info, "Options stored", 0, StoredValue},
	{Kind::Info, "Pushes received", 0, PushCountValue},
};

struct LevelDsc
{
	const char* pcTitle;
	const char* pcName; // what the console calls it
	uint32_t		rgbAccent;
	int			iParent; // -1 at the root: going up leaves the page
	const Item* paItems;
	int			iCount;
};

// One accent per level, because the eye is the only thing on screen big enough
// to say where you are without spelling it out.
constexpr LevelDsc LEVELS[LevelCount] = {
	{"SETTINGS",	 "root", 0x2E6FD8,			 -1,	  ROOT_ITEMS,	  static_cast<int>(std::size(ROOT_ITEMS))},
	{	 "UNITS",	 "units", 0x14A08C, LevelRoot,	 UNIT_ITEMS,	 static_cast<int>(std::size(UNIT_ITEMS))},
	{ "AZIMUTH", "azimuth", 0x7A5AD8, LevelRoot, AZIMUTH_ITEMS, static_cast<int>(std::size(AZIMUTH_ITEMS))},
	{	 "TIME",		"time", 0xC98A22, LevelRoot,	  TIME_ITEMS,	  static_cast<int>(std::size(TIME_ITEMS))},
	{	 "SYSTEM",  "system", 0xC94E6B, LevelRoot,	SYSTEM_ITEMS,  static_cast<int>(std::size(SYSTEM_ITEMS))},
};

// -----------------------------------------------------------------------------
//  The page
// -----------------------------------------------------------------------------

// LVGL's key codes, in the page's own terms.
//
// LV_KEY_NEXT and LV_KEY_PREV never reach an event handler while a group is
// attached -- LVGL spends them walking the group's focus -- but an encoder's
// long-press and a remapped keypad can still produce them, and they mean the
// same thing here as the arrows.
//
// bKnown:  set false for a key the page has no use for.
menu::Key FromLvKey(uint32_t uKey, bool& bKnown)
{
	bKnown = true;
	switch(uKey) {
	case LV_KEY_UP:	 return menu::Key::Up;
	case LV_KEY_DOWN:	 return menu::Key::Down;
	case LV_KEY_LEFT:	 return menu::Key::Left;
	case LV_KEY_RIGHT: return menu::Key::Right;
	case LV_KEY_PREV:	 return menu::Key::Up;
	case LV_KEY_NEXT:	 return menu::Key::Down;
	case LV_KEY_ENTER: return menu::Key::Enter;
	case LV_KEY_ESC:	 return menu::Key::Esc;
	default:				 break;
	}
	bKnown = false;
	return menu::Key::Esc;
}

// One row: the box, what it is, and what it is set to. The labels are children
// of the box, so deleting the box takes them with it -- which is what happens
// on every change of level.
struct Row
{
	explicit Row(lvgl::Object& parent) :
		box(parent),
		label(box, ""),
		value(box, "")
	{}

	Panel box;
	Label label;
	Label value;
};

class Page
{
public:
	bool Build();
	void Show();
	void Hide();

	void HandleKey(menu::Key eKey);
	void SetCloseHandler(menu::CloseHandler pfnClose) { m_pfnClose = pfnClose; }

	const char* LevelName() const { return LEVELS[m_iLevel].pcName; }
	int			Selection() const { return m_iSel; }

private:
	// Repaint the eye in the level's accent and re-letter the title.
	void DrawHeader();
	// Throw the rows away and build the level's own.
	void BuildRows();

	// Enter a level, or -- with the root's parent -- leave the page.
	void GoTo(int iLevel);
	void GoUp();

	// A tap on a row. iItem indexes the current level's table.
	void Activate(int iItem);

	// Activate m_iPending, then forget it. lv_async_call()'s trampoline.
	static void ActivatePending(void* pvPage);

	// Redraw the borders so the selected row is the one wearing the accent.
	void Highlight();

	// Move the selection by iDelta rows, around a ring that runs through the
	// title bar at -1.
	void SelectStep(int iDelta);

	// Do what tapping the selected row would do -- or, on the title bar, what
	// tapping that does.
	void ActivateSelected();

	// Point every keypad and encoder input device at this page's group, or
	// away from it. LVGL delivers a key to the focused object of the device's
	// group and to nothing at all without one, so this is the whole of what
	// makes the keys arrive here while the page is up and go nowhere while the
	// instruments are.
	void GrabKeys(bool bGrab);

	// Append the eye's outline to path, inset from the glass by fInset.
	void AppendLens(VectorPath& path, float fInset) const;

	std::optional<Screen>  m_screen;
	std::optional<DrawBuf> m_buf;
	std::optional<Canvas>  m_header;
	std::optional<Label>	  m_title;
	std::optional<Label>	  m_crumb;
	std::optional<Panel>	  m_list;

	// The screen is in this group only so that LVGL has somewhere to deliver a
	// key. Nothing else is ever added to it: the selection below is the page's
	// own, not the group's focus.
	std::optional<lvgl::Group> m_group;

	std::vector<Row> m_vRows;

	menu::CloseHandler m_pfnClose = nullptr;

	int32_t m_iCX	  = PANEL_R; // display centre, in its own pixels
	int32_t m_iCY	  = PANEL_R;
	int32_t m_iWidth = PANEL_R * 2;

	int m_iLevel = LevelRoot;

	// Which row the selection rests on, or -1 for the title bar. It starts and
	// returns there on every change of level, and while it is there no row is
	// marked at all -- a finger never moves it, and a page that showed a
	// highlight nobody asked for would just be noise.
	int m_iSel = -1;

	// The row a tap landed on, waiting for the event to finish. -1 when there
	// is none, which is also what a second async call left over from a double
	// tap finds.
	int m_iPending = -1;

	// Whether the page is the active screen. A key that arrived while it is
	// not -- the console can send one at any time -- must not walk a menu
	// nobody can see, and must certainly not call the close handler.
	bool m_bShown = false;
};

// -----------------------------------------------------------------------------
//  The eye
// -----------------------------------------------------------------------------

// Both lids as one closed outline, walked as line segments.
//
// lv_vector_path_append_arc() starts a subpath of its own, so the two lids
// cannot be two arcs -- they would fill as two separate shapes. Sixty-four
// segments over the upper lid leaves a sagitta of about a sixteenth of a
// pixel at this radius, which is well under what the rasteriser can show.
void Page::AppendLens(VectorPath& path, float fInset) const
{
	constexpr int LID_SEGMENTS	  = 64;
	constexpr int BELLY_SEGMENTS = 32;

	const float fCX = static_cast<float>(m_iCX);
	const float fCY = static_cast<float>(m_iCY);

	// Upper lid: the glass itself, inset. Its corners stay put as the inset
	// grows, so an inner lens is narrower rather than lower.
	const float fR	 = static_cast<float>(PANEL_R) - fInset;
	const float fDy = fCY - LENS_CORNER_Y;
	if(fR <= fDy)
		return;
	const float fHalf = std::sqrt(fR * fR - fDy * fDy);

	// Lower lid: the circle through both corners and the belly. Its radius
	// comes from the chord and the sagitta the ordinary way.
	const float fSag = (LENS_BELLY_Y - fInset) - LENS_CORNER_Y;
	if(fSag <= 0.0f)
		return;
	const float fR2  = (fHalf * fHalf + fSag * fSag) / (2.0f * fSag);
	const float fCY2 = LENS_CORNER_Y + fSag - fR2;

	// Left corner over the top to the right corner. Angles are measured from
	// three o'clock and grow clockwise, the way LVGL and ThorVG measure them.
	const float fA0 = std::atan2(LENS_CORNER_Y - fCY, -fHalf);
	const float fA1 = std::atan2(LENS_CORNER_Y - fCY, fHalf);
	for(int i = 0; i <= LID_SEGMENTS; i++) {
		const float a = fA0 + (fA1 - fA0) * static_cast<float>(i) / LID_SEGMENTS;
		const float x = fCX + fR * std::cos(a);
		const float y = fCY + fR * std::sin(a);
		if(i == 0)
			path.move_to(x, y);
		else
			path.line_to(x, y);
	}

	// Right corner under the belly and back to the left one. The first point
	// is the lid's last, so it is skipped.
	const float fB0 = std::atan2(LENS_CORNER_Y - fCY2, fHalf);
	const float fB1 = std::atan2(LENS_CORNER_Y - fCY2, -fHalf);
	for(int i = 1; i <= BELLY_SEGMENTS; i++) {
		const float a = fB0 + (fB1 - fB0) * static_cast<float>(i) / BELLY_SEGMENTS;
		path.line_to(fCX + fR2 * std::cos(a), fCY2 + fR2 * std::sin(a));
	}

	path.close();
}

// --------------------------------------------------------------------------

void Page::DrawHeader()
{
	const LevelDsc& level = LEVELS[m_iLevel];

	// Transparent, not the screen colour: outside the eye the canvas has to
	// let the page's own background through, corner to corner.
	m_header->fill_bg(Color(BG_COLOR), LV_OPA_TRANSP);

	lv_layer_t layer;
	m_header->init_layer(&layer);
	{
		VectorDraw dsc(&layer);
		VectorPath path(LV_VECTOR_PATH_QUALITY_HIGH);

		// The coloured ground: brightest in the middle of the eye, falling away
		// towards both corners, so the shape reads as lit rather than filled.
		const std::vector<lv_grad_stop_t> stops = {
			GradStop(Shade(level.rgbAccent, 0.34f), 0),
			GradStop(level.rgbAccent, 128),
			GradStop(Shade(level.rgbAccent, 0.34f), 255),
		};

		AppendLens(path, LENS_RIM);
		dsc.set_stroke_opa(LV_OPA_TRANSP);
		dsc.set_fill_linear_gradient(0.0f, 0.0f, static_cast<float>(m_iWidth), 0.0f);
		dsc.set_fill_gradient_stops(stops);
		dsc.set_fill_gradient_spread(LV_VECTOR_GRADIENT_SPREAD_PAD);
		dsc.set_fill_opa(LV_OPA_COVER);
		dsc.add_path(path);

		// The lid, a bright line right on the glass.
		path.clear();
		AppendLens(path, LENS_RIM);
		dsc.set_fill_opa(LV_OPA_TRANSP);
		dsc.set_stroke_color(Rgba(Shade(level.rgbAccent, 1.7f)));
		dsc.set_stroke_opa(LV_OPA_COVER);
		dsc.set_stroke_width(3.0f);
		dsc.set_stroke_join(LV_VECTOR_STROKE_JOIN_ROUND);
		dsc.add_path(path);

		// A second eye inside the first, in the same colour taken right down.
		// The title sits on this: on the bare gradient a white letter over the
		// bright middle has nothing to stand against.
		path.clear();
		AppendLens(path, LENS_RIM + LENS_INNER);
		dsc.set_stroke_opa(LV_OPA_TRANSP);
		dsc.set_fill_color(Rgba(Shade(level.rgbAccent, 0.30f)));
		dsc.set_fill_opa(LV_OPA_COVER);
		dsc.add_path(path);

		dsc.draw();
	}
	m_header->finish_layer(&layer);
	m_header->invalidate();

	m_title->set_text(level.pcTitle);
	m_title->style().text_color(Color(Shade(level.rgbAccent, 2.6f)));

	// One line under the title saying what a tap on the eye does, because the
	// eye is the only way back and nothing about it says so.
	if(level.iParent < 0)
		m_crumb->set_text("tap to leave");
	else
		m_crumb->set_text_fmt("< %s", LEVELS[level.iParent].pcTitle);
	m_crumb->style().text_color(Color(Shade(level.rgbAccent, 2.0f)));
}

// -----------------------------------------------------------------------------
//  The rows
// -----------------------------------------------------------------------------

void Page::BuildRows()
{
	const LevelDsc& level = LEVELS[m_iLevel];

	// Destroying a Row deletes its box, and LVGL takes the two labels with it;
	// the binding's own delete hook is what keeps their wrappers from following
	// a pointer that is already gone.
	m_vRows.clear();
	m_vRows.reserve(static_cast<size_t>(level.iCount));

	// A level that fits is centred in the band; one that does not fills it and
	// scrolls. Anchored at the top instead, every short level would leave the
	// bottom third of a round panel empty and look like a page that failed to
	// finish loading.
	const int32_t iNeeded = level.iCount * ROW_H + (level.iCount - 1) * ROW_GAP;
	const int32_t iHeight = iNeeded < LIST_H ? iNeeded : LIST_H;
	m_list->set_size(LIST_W, iHeight);
	m_list->align(Align::TopMid, 0, LIST_TOP + (LIST_H - iHeight) / 2);
	m_list->scroll_to(0, 0, lvgl::AnimEnable::Off);

	for(int i = 0; i < level.iCount; i++) {
		const Item& item = level.paItems[i];

		Row& row = m_vRows.emplace_back(*m_list);

		row.box.set_size(LIST_W, ROW_H);
		row.box.style()
			.bg_color(Color(ROW_BG))
			.bg_opa(Opacity::Cover)
			.radius(14)
			.pad_hor(20)
			.pad_ver(0)
			.border_color(Color(Shade(level.rgbAccent, 0.9f)))
			.border_width(2)
			.border_opa(item.eKind == Kind::Info ? Opacity::Opa30 : Opacity::Opa80);
		row.box.remove_flag(ObjFlag::Scrollable);

		// A read-only row is not a button and must not pretend to be one.
		if(item.eKind == Kind::Info) {
			row.box.remove_flag(ObjFlag::Clickable);
		}
		else {
			row.box.add_flag(ObjFlag::Clickable);
			row.box.style().state(State::Pressed).bg_color(Color(Shade(level.rgbAccent, 0.55f)));
			// A tap takes the selection with it: the two must not be able to
			// point at different rows.
			//
			// The row is not acted on here, though. Entering a submenu rebuilds
			// the rows, and the row being tapped is one of the objects that
			// would be deleted -- while LVGL is still walking its event list.
			// lv_async_call() runs the tap once the event is over.
			row.box.on_click([this, i](lvgl::Event&) {
				m_iSel = i;
				Highlight();
				m_iPending = i;
				lv_async_call(&Page::ActivatePending, this);
			});
		}

		row.label.set_text(item.pcLabel);
		row.label.style()
			.text_font(&lv_font_kanardia_20)
			.text_color(Color(item.eKind == Kind::Info ? ROW_DIMMED : ROW_LABEL));
		row.label.align(Align::LeftMid, 0, 0);

		// The right-hand side says what the row will do or what it is set to:
		// a value for the choices, the level ahead for a submenu, nothing for
		// an action -- its label already reads as one.
		std::string ssValue;
		if(item.pfnValue != nullptr)
			ssValue = item.pfnValue(item.iArg);
		else if(item.eKind == Kind::Submenu)
			ssValue = ">";

		row.value.set_text(ssValue.c_str());
		row.value.style()
			.text_font(&lv_font_kanardia_20)
			.text_color(Color(item.eKind == Kind::Info ? ROW_DIMMED : Shade(level.rgbAccent, 2.2f)));
		row.value.align(Align::RightMid, 0, 0);
	}
}

// --------------------------------------------------------------------------

void Page::Highlight()
{
	const LevelDsc& level = LEVELS[m_iLevel];

	for(int i = 0; i < static_cast<int>(m_vRows.size()); i++) {
		const bool	bOn  = (i == m_iSel);
		const Item& item = level.paItems[i];

		m_vRows[static_cast<size_t>(i)]
			.box.style()
			.border_color(Color(Shade(level.rgbAccent, bOn ? 2.2f : 0.9f)))
			.border_width(bOn ? 3 : 2)
			.border_opa(bOn ? Opacity::Cover : (item.eKind == Kind::Info ? Opacity::Opa30 : Opacity::Opa80));
	}

	// A level taller than the band can put the selection out of sight, and a
	// selection you cannot see is worse than none.
	if(m_iSel >= 0 && m_iSel < static_cast<int>(m_vRows.size()))
		m_vRows[static_cast<size_t>(m_iSel)].box.scroll_to_view(lvgl::AnimEnable::Off);
}

// --------------------------------------------------------------------------

void Page::SelectStep(int iDelta)
{
	// The title bar is part of the ring, at -1, which is why it is one longer
	// than the row count and why the arithmetic is done shifted up by one.
	const int iRing = LEVELS[m_iLevel].iCount + 1;
	const int iAt	 = ((m_iSel + 1 + iDelta) % iRing + iRing) % iRing;

	m_iSel = iAt - 1;
	Highlight();
}

// --------------------------------------------------------------------------

void Page::HandleKey(menu::Key eKey)
{
	if(m_bShown == false)
		return;

	switch(eKey) {
	// Left and right come from an encoder's two directions as readily as from a
	// keyboard, and on a list of rows they can only mean what up and down mean.
	case menu::Key::Up:
	case menu::Key::Left: SelectStep(-1); break;

	case menu::Key::Down:
	case menu::Key::Right: SelectStep(1); break;

	case menu::Key::Enter: ActivateSelected(); break;
	case menu::Key::Esc:	  GoUp(); break;
	}
}

// --------------------------------------------------------------------------

void Page::ActivateSelected()
{
	if(m_iSel < 0)
		GoUp();
	else
		Activate(m_iSel);
}

// --------------------------------------------------------------------------

void Page::ActivatePending(void* pvPage)
{
	Page*		 pPage	= static_cast<Page*>(pvPage);
	const int iItem	= pPage->m_iPending;
	pPage->m_iPending = -1;
	if(iItem >= 0)
		pPage->Activate(iItem);
}

// --------------------------------------------------------------------------

void Page::Activate(int iItem)
{
	const LevelDsc& level = LEVELS[m_iLevel];
	if(iItem < 0 || iItem >= level.iCount)
		return;

	const Item& item = level.paItems[iItem];
	switch(item.eKind) {
	case Kind::Submenu: GoTo(item.iArg); break;

	case Kind::Choice:
		if(item.pfnAction != nullptr)
			item.pfnAction(item.iArg);
		// Only this row's value can have moved, so only this row is rewritten.
		if(item.pfnValue != nullptr)
			m_vRows[static_cast<size_t>(iItem)].value.set_text(item.pfnValue(item.iArg).c_str());
		break;

	case Kind::Action:
		if(item.pfnAction != nullptr)
			item.pfnAction(item.iArg);
		// An action is the one thing here that can change what another row on
		// the same level says -- the counters under System -- so they all get
		// asked again.
		for(int i = 0; i < level.iCount; i++) {
			const Item& other = level.paItems[i];
			if(other.pfnValue != nullptr)
				m_vRows[static_cast<size_t>(i)].value.set_text(other.pfnValue(other.iArg).c_str());
		}
		break;

	case Kind::Info: break;
	}
}

// --------------------------------------------------------------------------

void Page::GoTo(int iLevel)
{
	if(iLevel < 0 || iLevel >= LevelCount)
		return;

	m_iLevel = iLevel;
	m_iSel	= -1;
	DrawHeader();
	BuildRows();
	APP_LOGI(TAG, "level -> %s", LEVELS[m_iLevel].pcName);
}

// --------------------------------------------------------------------------

void Page::GoUp()
{
	const int iParent = LEVELS[m_iLevel].iParent;
	if(iParent >= 0) {
		GoTo(iParent);
		return;
	}

	// Off the root, so the page is done. It does not put itself away, though:
	// the close handler loads whatever comes next and calls Hide() on the way,
	// so there is one path out however the page was left.
	if(m_pfnClose != nullptr)
		m_pfnClose();
}

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

bool Page::Build()
{
	lv_display_t* pDisplay = lv_display_get_default();
	m_iWidth					  = lv_display_get_horizontal_resolution(pDisplay);
	m_iCX						  = m_iWidth / 2;
	m_iCY						  = lv_display_get_vertical_resolution(pDisplay) / 2;

	m_screen.emplace();
	m_screen->style().bg_color(Color(BG_COLOR)).bg_opa(Opacity::Cover).pad_all(0).border_width(0);
	m_screen->remove_flag(ObjFlag::Scrollable);

	m_buf.emplace(static_cast<uint32_t>(m_iWidth), static_cast<uint32_t>(HEADER_H), lvgl::ColorFormat::ARGB8888);
	if(m_buf->raw() == nullptr) {
		APP_LOGE(
			TAG,
			"no room for a %dx%d ARGB8888 header (%d kB)",
			static_cast<int>(m_iWidth),
			static_cast<int>(HEADER_H),
			static_cast<int>(m_iWidth * HEADER_H * 4 / 1024)
		);
		return false;
	}

	m_header.emplace(*m_screen);
	m_header->set_draw_buf(m_buf->raw());
	m_header->align(Align::TopMid, 0, 0);
	// The eye is the way back, so it is the one widget on the page that is a
	// button without looking like one.
	m_header->add_flag(ObjFlag::Clickable);
	m_header->on_click([this](lvgl::Event&) { GoUp(); });

	m_title.emplace(*m_screen, "");
	m_title->style().text_font(&lv_font_kanardia_28);
	m_title->align(Align::TopMid, 0, 84);

	m_crumb.emplace(*m_screen, "");
	m_crumb->style().text_font(&lv_font_kanardia_16);
	m_crumb->align(Align::TopMid, 0, 124);

	m_list.emplace(*m_screen);
	m_list->set_size(LIST_W, LIST_H);
	m_list->align(Align::TopMid, 0, LIST_TOP);
	m_list->style().bg_opa(Opacity::Transparent).border_width(0).pad_all(0).pad_row(ROW_GAP);
	m_list->set_flex_flow(lvgl::FlexFlow::Column);
	m_list->set_flex_align(FlexAlign::Start, FlexAlign::Center, FlexAlign::Center);

	// The binding has no wrapper for either of these. A horizontal drag on a
	// list of rows is always an accident, and a scrollbar hanging off the right
	// edge of a round panel is half under the bezel.
	lv_obj_set_scroll_dir(m_list->raw(), LV_DIR_VER);
	lv_obj_set_scrollbar_mode(m_list->raw(), LV_SCROLLBAR_MODE_OFF);

	// Keys arrive through an LVGL group, because that is the only route there
	// is: a keypad input device with no group delivers a key to nothing. The
	// screen is in it purely to be the object LVGL can hand one to -- the page's
	// own selection is what actually moves, and the group's focus never does.
	m_group.emplace();
	m_group->add_obj(*m_screen);
	m_group->focus_obj(*m_screen);
	// ...so it must not take the theme's focused look either.
	m_screen->style().state(State::Focused).border_width(0).outline_width(0);
	m_screen->add_event_cb(lvgl::EventCode::Key, [this](lvgl::Event& e) {
		bool				 bKnown = false;
		const menu::Key eKey	  = FromLvKey(lv_event_get_key(e.raw()), bKnown);
		if(bKnown)
			HandleKey(eKey);
	});

	GoTo(LevelRoot);

	APP_LOGI(TAG, "settings page ready, %d levels", static_cast<int>(LevelCount));
	return true;
}

// --------------------------------------------------------------------------

void Page::GrabKeys(bool bGrab)
{
	if(m_group.has_value() == false)
		return;

	// Edit mode is what makes an encoder send its two directions as keys rather
	// than walk the group's focus -- and this group holds one object, so walking
	// it would do nothing at all.
	lv_group_set_editing(m_group->raw(), bGrab);

	lv_group_t* pGroup = bGrab ? m_group->raw() : nullptr;
	for(lv_indev_t* pIndev = lv_indev_get_next(nullptr); pIndev != nullptr; pIndev = lv_indev_get_next(pIndev)) {
		const lv_indev_type_t eType = lv_indev_get_type(pIndev);
		if(eType == LV_INDEV_TYPE_KEYPAD || eType == LV_INDEV_TYPE_ENCODER)
			lv_indev_set_group(pIndev, pGroup);
	}
}

// --------------------------------------------------------------------------

void Page::Show()
{
	// Always from the top, and always with the values re-read: an option can
	// have moved under us -- a parameter push, a unit changed over CAN -- while
	// the instruments were up.
	GoTo(LevelRoot);
	m_bShown = true;
	GrabKeys(true);
	m_screen->load();
}

// --------------------------------------------------------------------------

void Page::Hide()
{
	if(m_bShown == false)
		return;

	m_bShown = false;
	GrabKeys(false);

	// Whatever the pilot changed is dirty, and this is the one moment worth
	// spending a flash write on: saving per tap would stall the LVGL task inside
	// every single row.
	app::Model*		pModel	= app::GetModel();
	app::Settings& settings = app::GetSettings();
	if(pModel != nullptr && settings.IsOpen()) {
		const uint32_t uWritten = settings.Save(pModel->GetOptions());
		if(uWritten > 0)
			APP_LOGI(TAG, "wrote %u changed option blobs", static_cast<unsigned>(uWritten));
	}
}

Page g_page;

} // namespace

namespace menu {

bool CreatePage()
{
	return g_page.Build();
}

void Show()
{
	g_page.Show();
}

void Hide()
{
	g_page.Hide();
}

void HandleKey(Key eKey)
{
	platform::LockDisplay();
	g_page.HandleKey(eKey);
	platform::UnlockDisplay();
}

void SetCloseHandler(CloseHandler pfnClose)
{
	g_page.SetCloseHandler(pfnClose);
}

const char* LevelName()
{
	return g_page.LevelName();
}

int Selection()
{
	return g_page.Selection();
}

} // namespace menu
