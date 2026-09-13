/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// app::BlobStore on a directory of files.
//
// The board keeps one NVS entry per key; the simulator keeps one file per key,
// which is the same shape in the place a desktop program keeps such a thing:
//
//     $ESPP4_SIM_STATE, or $XDG_STATE_HOME/espp4-sim,
//     or $HOME/.local/state/espp4-sim
//
// Keys are the short ASCII names Settings spells -- `opt_<number>` and
// `params` -- so a file name is the key with `.blob` after it and nothing has
// to be escaped.
//
// Writes go to a temporary file and are renamed into place, which is the
// desktop equivalent of what NVS's per-entry CRC buys on the board: an
// interrupted write leaves the previous value, never half of the new one.
// Commit() therefore has nothing to do.

#include "BlobStore.h"
#include "Platform.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace app {

namespace {

	constexpr const char* TAG = "settings";

	constexpr const char* SUFFIX = ".blob";

// Where the settings live, first of these that is set:
//
//   ESPP4_SIM_STATE   an explicit directory, which is what a test harness
//                     passes so a run starts from a known state;
//   XDG_STATE_HOME    the freedesktop location for state that should survive
//                     a restart but is not configuration the user edits;
//   HOME              the same, spelled out.
	std::filesystem::path StateDir()
	{
		if(const char* pszDir = std::getenv("ESPP4_SIM_STATE"); pszDir != nullptr && *pszDir != '\0')
			return std::filesystem::path(pszDir);

		if(const char* pszXdg = std::getenv("XDG_STATE_HOME"); pszXdg != nullptr && *pszXdg != '\0')
			return std::filesystem::path(pszXdg) / "espp4-sim";

		const char* pszHome = std::getenv("HOME");
		return std::filesystem::path(pszHome != nullptr ? pszHome : ".") / ".local" / "state" / "espp4-sim";
	}

	class BlobStoreFile final : public BlobStore
	{
	public:
		bool Open() override
		{
			if(m_bOpen)
				return true;

			m_dir = StateDir();

			std::error_code ec;
			std::filesystem::create_directories(m_dir, ec);
			if(ec && std::filesystem::is_directory(m_dir) == false) {
				APP_LOGE(TAG, "cannot use '%s': %s", m_dir.c_str(), ec.message().c_str());
				return false;
			}

			m_bOpen = true;
			APP_LOGI(TAG, "settings directory '%s'", m_dir.c_str());
			return true;
		}

		void Close() override { m_bOpen = false; }

		bool IsOpen() const override { return m_bOpen; }

		bool Read(const char* pcKey, common::BLOB& blob) const override
		{
			if(m_bOpen == false)
				return false;

			const std::filesystem::path path = PathFor(pcKey);

			std::error_code ec;
			const auto		 uSize = std::filesystem::file_size(path, ec);
			if(ec)
				return false;  // Never stored. Normal on a first run.

			std::ifstream in(path, std::ios::binary);
			if(in.is_open() == false) {
				APP_LOGW(TAG, "cannot read '%s'", path.c_str());
				return false;
			}

			blob.resize(static_cast<size_t>(uSize));
			in.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(uSize));
			if(in.good() == false && in.eof() == false) {
				APP_LOGW(TAG, "short read on '%s'", path.c_str());
				blob.clear();
				return false;
			}

			blob.resize(static_cast<size_t>(in.gcount()));
			return blob.empty() == false;
		}

		bool Write(const char* pcKey, common::SpanBLOB blob, bool /*bCommit*/) override
		{
			if(m_bOpen == false || blob.empty())
				return false;

			const std::filesystem::path path = PathFor(pcKey);
			std::filesystem::path		 tmp	= path;
			tmp += ".new";

			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				if(out.is_open() == false) {
					APP_LOGE(TAG, "cannot write '%s'", tmp.c_str());
					return false;
				}
				out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
				if(out.good() == false) {
					APP_LOGE(TAG, "short write on '%s'", tmp.c_str());
					return false;
				}
			}

			std::error_code ec;
			std::filesystem::rename(tmp, path, ec);
			if(ec) {
				APP_LOGE(TAG, "cannot replace '%s': %s", path.c_str(), ec.message().c_str());
				std::filesystem::remove(tmp, ec);
				return false;
			}
			return true;
		}

		// Every write is already in place by the time it returns.
		bool Commit() override { return m_bOpen; }

		bool Erase() override
		{
			if(m_bOpen == false)
				return false;

			std::error_code ec;
			for(const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
				if(entry.path().extension() == SUFFIX)
					std::filesystem::remove(entry.path(), ec);
			}
			return !ec;
		}

		// Entries stored, twice: a directory has no fixed capacity to report,
		// and the console prints used/total. Whether the store is open at all
		// is a separate question there, answered by Settings::IsOpen().
		Usage GetUsage() const override
		{
			if(m_bOpen == false)
				return {};

			size_t			 uCount = 0;
			std::error_code ec;
			for(const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
				if(entry.path().extension() == SUFFIX)
					++uCount;
			}
			return {uCount, 0, uCount};
		}

	private:
		std::filesystem::path PathFor(const char* pcKey) const { return m_dir / (std::string(pcKey) + SUFFIX); }

		std::filesystem::path m_dir;
		bool						 m_bOpen = false;
	};

} // namespace

// --------------------------------------------------------------------------

BlobStore& GetBlobStore()
{
	static BlobStoreFile store;
	return store;
}

} // namespace app
