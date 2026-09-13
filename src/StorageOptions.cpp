/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "StorageOptions.h"

#include "AppOptions.h"
#include "Platform.h"

#include "Parameter/ParamContainer.h"
#include "Parameter/ParamStorage.h"

#include <cstdio>

namespace app {

namespace {

	constexpr const char* TAG = "settings";

// The entry holding the whole parameter container, as one packed blob.
	constexpr const char* PARAM_ENTRY = "params";

// Longest key a store has to cope with, including the terminator. NVS allows
// 15 characters and that is the tighter of the two limits, so it is the one
// this side respects.
	constexpr size_t KEY_MAX = 16;

// Entry name for an option key.
//
// The key's *number* goes in, not its name: enumerator names may be renamed,
// the numbers may not -- they are what every other Kanardia product stores in
// its own flash image, and what `Container::Load()` matches on. "opt_65535" is
// nine characters, so it fits anywhere.
	void MakeEntryName(char* szOut, size_t uSize, option::Key eKey)
	{
		std::snprintf(szOut, uSize, "opt_%u", static_cast<unsigned>(eKey));
	}

} // namespace

// --------------------------------------------------------------------------

Settings::Settings() :
	m_store(GetBlobStore())
{}

// --------------------------------------------------------------------------

Settings::~Settings()
{
	Close();
}

// --------------------------------------------------------------------------

bool Settings::Open()
{
	if(m_store.Open() == false)
		return false;

	const Usage use = GetUsage();
	APP_LOGI(
		TAG, "settings open: %u of %u entries used", static_cast<unsigned>(use.uUsed), static_cast<unsigned>(use.uTotal)
	);
	return true;
}

// --------------------------------------------------------------------------

void Settings::Close()
{
	m_store.Close();
}

// --------------------------------------------------------------------------

bool Settings::IsOpen() const
{
	return m_store.IsOpen();
}

// --------------------------------------------------------------------------

uint32_t Settings::Load(Options& options)
{
	if(IsOpen() == false)
		return 0;

	uint32_t		 uCount = 0;
	common::BLOB blob;
	char			 szName[KEY_MAX];

	for(const option::Key eKey : options.GetKeys()) {
		MakeEntryName(szName, sizeof(szName), eKey);
		if(ReadBlob(szName, blob) == false)
			continue;	 // Never stored. Normal on a first boot.

		options.SetBLOB(eKey, blob);
		// Unpack() clears the flag itself in every serializer we register, but
		// a value that came out of the store is by definition not pending.
		options.ClearDirty(eKey);
		++uCount;

		APP_LOGD(TAG, "loaded %s, %u B", szName, static_cast<unsigned>(blob.size()));
	}

	return uCount;
}

// --------------------------------------------------------------------------

uint32_t Settings::Save(Options& options, bool bDirtyOnly)
{
	if(IsOpen() == false)
		return 0;

	uint32_t uCount = 0;
	char		szName[KEY_MAX];

	for(const option::Key eKey : options.GetKeys()) {
		if(bDirtyOnly && options.IsDirty(eKey) == false)
			continue;

		const common::BLOB blob = options.GetBLOB(eKey);
		if(blob.empty()) {
			// Pack() failed, or the key is not registered after all. Writing
			// nothing would look like "stored and empty" on the next Load.
			APP_LOGW(TAG, "option %u packed to nothing, not stored", static_cast<unsigned>(eKey));
			continue;
		}

		MakeEntryName(szName, sizeof(szName), eKey);
		if(WriteBlob(szName, blob, false) == false)
			continue;

		options.ClearDirty(eKey);
		++uCount;

		APP_LOGD(TAG, "stored %s, %u B", szName, static_cast<unsigned>(blob.size()));
	}

	if(uCount > 0 && Commit() == false)
		return 0;

	return uCount;
}

// --------------------------------------------------------------------------

bool Settings::LoadParameters(parameter::ParameterContainer& pc)
{
	common::BLOB blob;
	if(ReadBlob(PARAM_ENTRY, blob) == false)
		return false;	  // Never stored. Normal on a first boot.

	parameter::ParamStorage storage;
	storage.Load(blob);

	// Load() answers nothing on a bad CRC or a failed decompression, and
	// ApplyTo() would then silently leave every parameter at its default --
	// which looks identical to "no blob". Prove the blob names at least one
	// parameter we actually hold before trusting it.
	bool bAnyKnown	  = false;
	auto [it, itEnd] = pc.GetIterators();
	for(; it != itEnd && bAnyKnown == false; ++it)
		bAnyKnown = storage.GetCount(it->first) > 0;

	if(bAnyKnown == false) {
		APP_LOGW(TAG, "parameter blob (%u B) did not unpack", static_cast<unsigned>(blob.size()));
		return false;
	}

	storage.ApplyTo(pc);
	APP_LOGI(TAG, "parameters restored from a %u B blob", static_cast<unsigned>(blob.size()));
	return true;
}

// --------------------------------------------------------------------------

bool Settings::SaveParameters(const parameter::ParameterContainer& pc)
{
	// ParamStorage builds into a 32 kB flatbuffer builder before compressing,
	// so this is a transient allocation of that size -- large enough that the
	// PSRAM heap takes it on the board, which is where it belongs.
	const common::BLOB blob = parameter::ParamStorage::Save(pc);
	if(blob.empty()) {
		APP_LOGE(TAG, "parameter container packed to nothing");
		return false;
	}

	if(WriteBlob(PARAM_ENTRY, blob) == false)
		return false;

	APP_LOGI(TAG, "%d parameters stored as a %u B blob", pc.GetCount(), static_cast<unsigned>(blob.size()));
	return true;
}

// --------------------------------------------------------------------------

bool Settings::ReadBlob(const char* pcKey, common::BLOB& blob) const
{
	return m_store.Read(pcKey, blob);
}

// --------------------------------------------------------------------------

bool Settings::WriteBlob(const char* pcKey, common::SpanBLOB blob, bool bCommit)
{
	// A store refuses a zero-length blob, so the caller has to mean something.
	if(blob.empty())
		return false;

	return m_store.Write(pcKey, blob, bCommit);
}

// --------------------------------------------------------------------------

bool Settings::Commit()
{
	return m_store.Commit();
}

// --------------------------------------------------------------------------

bool Settings::Erase()
{
	return m_store.Erase();
}

// --------------------------------------------------------------------------

Settings::Usage Settings::GetUsage() const
{
	return m_store.GetUsage();
}

// --------------------------------------------------------------------------

Settings& GetSettings()
{
	static Settings settings;
	return settings;
}

} // namespace app
