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

// platform::FirmwareTarget on ESP-IDF's OTA API.
//
// A firmware update pushed over CAN lands in whichever app slot is not
// running -- ota_0 and ota_1 in partitions.csv -- and becomes the boot
// partition once the whole image validates. Nothing here restarts the board;
// that is the operator's call, and the next reset is soon enough.

#include "Platform.h"

#include "esp_ota_ops.h"

namespace platform {

namespace {

	constexpr const char* TAG = "ota";

	class FirmwareEsp final : public FirmwareTarget
	{
	public:
		bool Begin(uint32_t /*uPages*/) override
		{
			Abort();

			// Never the slot we are executing from. On a board flashed over USB
			// that is ota_0, so the first update lands in ota_1 and they
			// alternate.
			m_pPart = esp_ota_get_next_update_partition(nullptr);
			if(m_pPart == nullptr) {
				APP_LOGE(TAG, "no OTA slot to write to");
				return false;
			}

			// OTA_SIZE_UNKNOWN erases lazily, page by page, instead of erasing
			// four megabytes up front while the sender waits for us to ask for
			// page 0.
			const esp_err_t err = esp_ota_begin(m_pPart, OTA_SIZE_UNKNOWN, &m_hOta);
			if(err != ESP_OK) {
				APP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
				m_hOta  = 0;
				m_pPart = nullptr;
				return false;
			}

			APP_LOGI(TAG, "writing %s @ 0x%06x", m_pPart->label, static_cast<unsigned>(m_pPart->address));
			return true;
		}

		bool Write(const uint8_t* pData, uint32_t uSize) override
		{
			if(m_hOta == 0)
				return false;

			const esp_err_t err = esp_ota_write(m_hOta, pData, uSize);
			if(err != ESP_OK) {
				APP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
				return false;
			}
			return true;
		}

		bool Finish() override
		{
			if(m_hOta == 0)
				return false;

			// esp_ota_end() validates the image header and checksum of what
			// actually landed in flash.
			esp_err_t err = esp_ota_end(m_hOta);
			m_hOta		  = 0;
			if(err != ESP_OK) {
				APP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
				m_pPart = nullptr;
				return false;
			}

			err = esp_ota_set_boot_partition(m_pPart);
			if(err != ESP_OK) {
				APP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
				m_pPart = nullptr;
				return false;
			}

			m_pPart = nullptr;
			return true;
		}

		void Abort() override
		{
			if(m_hOta != 0) {
				esp_ota_abort(m_hOta);
				m_hOta = 0;
			}
			m_pPart = nullptr;
		}

		const char* GetName() const override { return m_pPart != nullptr ? m_pPart->label : "(no slot)"; }

		bool IsOpen() const override { return m_hOta != 0; }

	private:
		esp_ota_handle_t		  m_hOta	 = 0;
		const esp_partition_t* m_pPart = nullptr;
	};

} // namespace

// --------------------------------------------------------------------------

FirmwareTarget& GetFirmwareTarget()
{
	static FirmwareEsp target;
	return target;
}

} // namespace platform
