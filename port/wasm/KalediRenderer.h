/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// The whole of what the Kaledi layout editor asks of this module.
//
// One object, held for as long as the editor is open. It carries the
// parameters the aircraft has and the values they are standing at, and answers
// Render() with a transparent pixmap of one item. Parameters and values both
// change during the run -- that is the point of an editor -- so nothing here
// is fixed at construction except the machinery.
//
// The parameters are Common's own container, built by Common's own loader.
// That matters more than it looks: a parameter carries its function, its
// units, its bands, its names and its filter, and getting any of them wrong
// would show the operator a widget that is not the one the panel will draw.
// app::Parameters is no use here -- it hardcodes the four parameters this
// firmware happens to show -- but parameter::ParameterLoaderBase is exactly
// the path Nesis takes, and it reads the same blobs the rest of the Kanardia
// tooling writes.
//
// A value is written into a can::DirectNOD and read back out through the
// parameter's own callback, which is the path a frame off the bus takes on the
// board. Nothing here pretends to be a bus; it is simply where a parameter
// looks, so using it means the editor exercises the same code the panel does.
//
// The container is handed to item::MakeItem() on every render rather than
// registered anywhere, which is what lets an editor hold more than one of
// these -- one per aircraft, say -- without them treading on each other.

#include "LvglHeadless.h"

#include "Item/ItemPanel.h"

#include "CanAerospace/CanNOD.h"
#include "Option/OptionUnits.h"
#include "Parameter/ParamContainer.h"
#include "Parameter/ParamStorage.h"

#include <emscripten/val.h>

#include <cstdint>
#include <string>
#include <vector>

namespace kaledi {

class Renderer
{
public:
	Renderer();

	Renderer(const Renderer&)				 = delete;
	Renderer& operator=(const Renderer&) = delete;

	// One parameter::fbs::ParamItem flatbuffer -- what
	// ParamStorage::GetParameterFB() produces, and what a Kanardia tool pushes
	// at a unit over CAN. The blob's can_id picks the parameter; if the
	// container does not hold it yet it is created from Common's own defaults
	// first, so a single call is enough to introduce a parameter this module
	// has never seen.
	//
	// Returns the can::Id it named, or 0 when the blob is not one we can hold.
	int SetParameter(const std::string& sBlob);

	// The packed, LZO-compressed whole-container blob ParamStorage::Save()
	// writes -- one aircraft's entire parameter set, the form the settings
	// store and the rest of the tooling keep it in.
	//
	// The container is rebuilt from Common's default table with the blob laid
	// over it, so a parameter the blob does not mention is still there, at its
	// defaults, rather than missing. That is what the products do and it is
	// what lets an editor show a panel for an aircraft whose configuration is
	// half written.
	//
	// Returns how many parameters the blob itself supplied. Zero means it
	// supplied none -- a bad CRC, bad compression or the wrong kind of blob,
	// all of which ParamStorage::Load() answers silently -- and leaves the
	// container holding the defaults, exactly as LoadDefaults() would.
	int SetParameters(const std::string& sBlob);

	// Every parameter Common knows, at its default bands, names and units, and
	// no stored blob over them. What an editor has to show before an aircraft
	// has been chosen, and the quickest way to see that this module works.
	//
	// Returns how many parameters the container holds.
	int LoadDefaults();

	// The value the next pixmap is drawn with, in the parameter's *system*
	// unit: m/s for an airspeed, metres for an altitude, rpm for an engine.
	// That is what the bands are stored in and what a CANaerospace frame
	// carries; the readout's own unit is the parameter's business.
	//
	// Returns false when no parameter holds that id.
	bool SetValue(int iCanId, float fValue);

	// Every can::Id the container holds, as a JSON array of
	// {"id":500,"name":"Engine RPM","unit":"RPM"}. What an editor populates a
	// parameter picker from.
	std::string GetParameters() const;

	// Any subset of item::Style as JSON -- see KalediConfig.h. Returns false
	// and changes nothing if the JSON is bad.
	bool SetStyle(const std::string& sJson);

	// One item as JSON, as a w*h*4 byte RGBA pixmap, transparent wherever the
	// item drew nothing. Empty when the request could not be drawn, with the
	// reason in GetLastError().
	emscripten::val Render(const std::string& sJson);

	// Why the last call that could fail did. Empty after one that did not.
	std::string GetLastError() const { return m_ssError; }

private:
	// Build the container from Common's default table with whatever m_storage
	// currently holds laid over it. Returns how many parameters the storage
	// supplied, which is zero when it holds nothing.
	int RebuildParameters();

	// How many real parameters the container holds.
	//
	// Not ParameterContainer::GetCount(), which counts two things that are not
	// parameters: Common's default table opens with an Id::Invalid
	// "Placeholder" row, and Find() files a default-constructed dummy under
	// that same id whenever it is asked for something the container does not
	// hold. Neither can be drawn and neither is listed by GetParameters(), so
	// neither should be counted.
	int CountHeld() const;

private:
	// Where a parameter reads its value from, and where SetValue() writes it.
	::can::DirectNOD m_nod;

	// The unit preferences the loader hands each parameter. Defaults, until
	// there is a reason for the editor to say otherwise.
	::option::Units m_units;

	::parameter::ParameterContainer m_pc;

	// Holds the bytes m_pc was built from: ParamStorage::Load() keeps a
	// flatbuffer pointer into its own decompressed buffer, so it has to
	// outlive every use of it.
	::parameter::ParamStorage m_storage;

	::item::Style m_style;

	wasm::Surface m_surface;

	std::string m_ssError;

	// The swizzled copy handed to JavaScript, kept so that the buffer is
	// allocated once per size rather than once per frame.
	std::vector<uint8_t> m_vRgba;
};

} // namespace kaledi
