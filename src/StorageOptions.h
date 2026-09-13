/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2019 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *   Writen by:                                                            *
 *      Rok Markovic [rok.markovic@kanardia.eu]                            *
 *                                                                         *
 *   Status: Open Source                                                   *
 *                                                                         *
 *   License: GPL - GNU General Public License                             *
 *                                                                         *
 ***************************************************************************/

#pragma once

// The options and the parameter set, kept between boots.
//
// Common already knows how to turn an option into a blob: every registered
// `option::Key` has a `Serialize` that packs it into a flatbuffer, and
// `Container::GetBLOB()` / `SetBLOB()` are the two ends of that. All a product
// has to add is somewhere to put the bytes -- which is `app::BlobStore`, and
// the only part of this that differs between the board and the simulator.
// Everything in this file is shared.
//
// That is deliberately *not* `Container::Save()`, which packs every option into
// one flat image with a size and a CRC in front. That shape suits the raw
// flash and EEPROM the other products write to; here it would mean rewriting
// every option to change one, and re-implementing what the store already does.

#include "KanardiaCommon.h"

#include "BlobStore.h"

#include <cstddef>
#include <cstdint>

namespace parameter { class ParameterContainer; }

namespace app {

class Options;

class Settings
{
public:
	// What the console reports and what a port fills in.
	using Usage = BlobStore::Usage;

	Settings();
	Settings(const Settings&)				 = delete;
	Settings& operator=(const Settings&) = delete;
	~Settings();

	// Open the store this build keeps its settings in.
	//
	// Returns false if there is none, after which every other call does
	//         nothing and answers false.
	bool Open();
	void Close();
	bool IsOpen() const;

	// Read every registered option that has an entry in the store.
	//
	// Missing keys are left at their defaults, which is the normal state on a
	// first boot -- not an error.
	//
	// Returns how many were actually read.
	uint32_t Load(Options& options);

	// Write the options back and commit once.
	//
	// bDirtyOnly:  the usual case: only options that changed. Pass false
	//              to force the whole set out, which is what populates a
	//              fresh partition.
	// Returns how many entries were written.
	uint32_t Save(Options& options, bool bDirtyOnly = true);

	// --- The parameter set ----------------------------------------------

	// Restore the instrument parameters from their blob.
	//
	// Unlike the options, the whole container is one entry: `ParamStorage`
	// packs every parameter into a single flatbuffer and LZO-compresses it,
	// which is the form the rest of the Kanardia tooling reads and writes, and
	// splitting it per can::Id would make the blob non-portable.
	//
	// The container must already hold the parameters -- `ApplyTo()` fills in
	// the ones it recognises and ignores the rest.
	//
	// Returns false if nothing was stored, or the blob would not decompress.
	bool LoadParameters(parameter::ParameterContainer& pc);

	// Pack the container and write it. Returns false if nothing was written.
	bool SaveParameters(const parameter::ParameterContainer& pc);

	// --- Raw blobs, for anything that is not an option ------------------

	bool ReadBlob(const char* pcKey, common::BLOB& blob) const;
	// bCommit:  false batches the write; call Commit() yourself.
	bool WriteBlob(const char* pcKey, common::SpanBLOB blob, bool bCommit = true);
	bool Commit();

	// Drop everything in the store. Takes effect immediately.
	bool Erase();

	Usage GetUsage() const;

private:
	BlobStore& m_store;
};

// --------------------------------------------------------------------------

// The one settings store.
Settings& GetSettings();

} // namespace app
