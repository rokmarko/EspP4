/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// platform::FirmwareTarget on a file.
//
// A desktop has no second app slot to write, but the CAN side of a firmware
// push is worth exercising anyway: APS_B's page buffering, the page CRC32, the
// retries and the commit are the same code on both builds, and this is the one
// place they can be driven without risking a board. The image lands next to
// the settings as `firmware.bin`, so a transfer can be diffed against what was
// sent.
//
// Nothing ever runs it. Finish() checks that the byte count matches what the
// sender announced and says so; that is as much of a verdict as this side can
// give.

#include "Platform.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace platform {

namespace {

	constexpr const char* TAG = "ota";

// One 2 kB page, as APS_B counts them.
	constexpr uint32_t PAGE_SIZE = 2048;

// Beside the settings -- see port/pc/BlobStoreFile.cpp, which spells the same
// three-step lookup. Duplicated rather than shared because these are the only
// two files the simulator writes and a header for one constant each would say
// less than the comment does.
	std::filesystem::path StateDir()
	{
		if(const char* pszDir = std::getenv("ESPP4_SIM_STATE"); pszDir != nullptr && *pszDir != '\0')
			return std::filesystem::path(pszDir);

		if(const char* pszXdg = std::getenv("XDG_STATE_HOME"); pszXdg != nullptr && *pszXdg != '\0')
			return std::filesystem::path(pszXdg) / "espp4-sim";

		const char* pszHome = std::getenv("HOME");
		return std::filesystem::path(pszHome != nullptr ? pszHome : ".") / ".local" / "state" / "espp4-sim";
	}

	class FirmwarePc final : public FirmwareTarget
	{
	public:
		~FirmwarePc() override { Abort(); }

		bool Begin(uint32_t uPages) override
		{
			Abort();

			const std::filesystem::path dir = StateDir();

			std::error_code ec;
			std::filesystem::create_directories(dir, ec);

			m_ssPath = (dir / "firmware.bin").string();
			m_pFile	= std::fopen(m_ssPath.c_str(), "wb");
			if(m_pFile == nullptr) {
				APP_LOGE(TAG, "cannot write '%s'", m_ssPath.c_str());
				return false;
			}

			m_uExpected = static_cast<uint64_t>(uPages) * PAGE_SIZE;
			m_uWritten	= 0;
			APP_LOGI(TAG, "writing '%s'", m_ssPath.c_str());
			return true;
		}

		bool Write(const uint8_t* pData, uint32_t uSize) override
		{
			if(m_pFile == nullptr)
				return false;

			if(std::fwrite(pData, 1, uSize, m_pFile) != uSize) {
				APP_LOGE(TAG, "short write on '%s'", m_ssPath.c_str());
				return false;
			}
			m_uWritten += uSize;
			return true;
		}

		bool Finish() override
		{
			if(m_pFile == nullptr)
				return false;

			std::fclose(m_pFile);
			m_pFile = nullptr;

			if(m_uWritten != m_uExpected) {
				APP_LOGE(
					TAG,
					"'%s' is %llu B, the sender announced %llu B",
					m_ssPath.c_str(),
					static_cast<unsigned long long>(m_uWritten),
					static_cast<unsigned long long>(m_uExpected)
				);
				return false;
			}
			return true;
		}

		void Abort() override
		{
			if(m_pFile != nullptr) {
				std::fclose(m_pFile);
				m_pFile = nullptr;
			}
			m_uWritten	= 0;
			m_uExpected = 0;
		}

		const char* GetName() const override { return m_ssPath.empty() ? "(no file)" : m_ssPath.c_str(); }

		bool IsOpen() const override { return m_pFile != nullptr; }

	private:
		std::FILE*	m_pFile = nullptr;
		std::string m_ssPath;
		uint64_t		m_uWritten	= 0;
		uint64_t		m_uExpected = 0;
	};

} // namespace

// --------------------------------------------------------------------------

FirmwareTarget& GetFirmwareTarget()
{
	static FirmwarePc target;
	return target;
}

} // namespace platform
