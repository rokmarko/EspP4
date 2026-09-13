/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// Where a packed option or parameter blob is kept between boots.
//
// Common already knows how to turn an option into a blob -- every registered
// `option::Key` has a `Serialize` behind `Container::GetBLOB()` / `SetBLOB()`
// -- so what a product has to supply is a key/blob store and nothing more.
// `app::Settings` in StorageOptions.h is written against this interface, which
// is why the whole of the settings logic is shared between the two builds.
//
// The board's store is the `settings` NVS partition: a key/blob store with its
// own wear levelling and per-entry CRC, which is why the two meet directly
// with no framing of our own. The simulator's is a directory of files, one per
// key, under the user's state directory -- the same shape, in the place a
// desktop program keeps such a thing.
//
// Keys are short ASCII names, at most 15 characters, because that is what NVS
// allows. `Settings` spells them `opt_<number>` and `params`.

#include "KanardiaCommon.h"

#include "BLOB/BLOB.h"

#include <cstddef>

namespace app {

class BlobStore
{
public:
	struct Usage
	{
		size_t uUsed  = 0;
		size_t uFree  = 0;
		size_t uTotal = 0;
	};

	virtual ~BlobStore() = default;

	// Mount the store and open it for reading and writing.
	//
	// An unreadable store is wiped rather than reported: neither a blank NVS
	// partition nor a settings directory written by a newer build can be read
	// back, and losing the options costs nothing but their defaults.
	//
	// Returns false if there is no usable store, after which every other call
	//         does nothing and answers false.
	virtual bool Open()			 = 0;
	virtual void Close()			 = 0;
	virtual bool IsOpen() const = 0;

	// Missing keys are not an error -- that is the normal state on a first
	// boot -- and answer false with blob left alone.
	virtual bool Read(const char* pcKey, common::BLOB& blob) const = 0;

	// bCommit:  false batches the write; the caller calls Commit().
	virtual bool Write(const char* pcKey, common::SpanBLOB blob, bool bCommit) = 0;
	virtual bool Commit()																		= 0;

	// Drop every key. Takes effect immediately.
	virtual bool Erase() = 0;

	virtual Usage GetUsage() const = 0;
};

// --------------------------------------------------------------------------

// The one store this build keeps its settings in, supplied by the port.
BlobStore& GetBlobStore();

} // namespace app
