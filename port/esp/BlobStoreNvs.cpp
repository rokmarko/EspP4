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

// app::BlobStore on NVS.
//
// The store lives in its own `settings` partition (see `partitions.csv`) so it
// never competes for space with the Wi-Fi calibration data IDF keeps in the
// default `nvs`. NVS is a key/blob store with its own wear levelling and
// per-entry CRC, so the interface and the API meet directly, with no framing
// of our own.
//
// Mounting an NVS partition costs internal RAM in proportion to its size and
// never gives it back -- see the note in partitions.csv before enlarging it.
// Trying to log that cost from here does not work: the console, CAN and LVGL
// tasks are all allocating at the same time, and the before/after delta
// measures them too. app_main's "largest block" line is the number that
// matters.

#include "BlobStore.h"
#include "Platform.h"

#include "nvs.h"
#include "nvs_flash.h"

namespace app {

namespace {

	constexpr const char* TAG = "settings";

// Partition label, as spelled in partitions.csv.
	constexpr const char* PARTITION = "settings";
// Namespace inside it. Room for others alongside.
	constexpr const char* NAMESPACE = "option";

	class BlobStoreNvs final : public BlobStore
	{
	public:
		~BlobStoreNvs() override { Close(); }

		bool Open() override
		{
			if(m_bOpen)
				return true;

			esp_err_t err = nvs_flash_init_partition(PARTITION);
			if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
				// Blank flash, or an NVS layout written by a newer IDF. Neither
				// can be read; the options are worth less than the boot, so wipe
				// and go on.
				APP_LOGW(TAG, "re-initialising '%s' partition: %s", PARTITION, esp_err_to_name(err));
				if(nvs_flash_erase_partition(PARTITION) != ESP_OK) {
					APP_LOGE(TAG, "could not erase '%s'", PARTITION);
					return false;
				}
				err = nvs_flash_init_partition(PARTITION);
			}
			if(err != ESP_OK) {
				// Almost always a partition table that does not carry `settings`.
				APP_LOGE(TAG, "no usable '%s' partition: %s", PARTITION, esp_err_to_name(err));
				return false;
			}

			err = nvs_open_from_partition(PARTITION, NAMESPACE, NVS_READWRITE, &m_hNvs);
			if(err != ESP_OK) {
				APP_LOGE(TAG, "nvs_open('%s'): %s", NAMESPACE, esp_err_to_name(err));
				return false;
			}

			m_bOpen = true;
			return true;
		}

		void Close() override
		{
			if(m_bOpen == false)
				return;
			nvs_close(m_hNvs);
			m_hNvs  = 0;
			m_bOpen = false;
		}

		bool IsOpen() const override { return m_bOpen; }

		bool Read(const char* pcKey, common::BLOB& blob) const override
		{
			if(m_bOpen == false)
				return false;

			// Two calls: the first asks how big the entry is, the second reads it.
			size_t	 uSize = 0;
			esp_err_t err	 = nvs_get_blob(m_hNvs, pcKey, nullptr, &uSize);
			if(err != ESP_OK) {
				if(err != ESP_ERR_NVS_NOT_FOUND)
					APP_LOGW(TAG, "nvs_get_blob('%s') size: %s", pcKey, esp_err_to_name(err));
				return false;
			}

			blob.resize(uSize);
			err = nvs_get_blob(m_hNvs, pcKey, blob.data(), &uSize);
			if(err != ESP_OK) {
				APP_LOGW(TAG, "nvs_get_blob('%s'): %s", pcKey, esp_err_to_name(err));
				blob.clear();
				return false;
			}

			blob.resize(uSize);
			return true;
		}

		bool Write(const char* pcKey, common::SpanBLOB blob, bool bCommit) override
		{
			// NVS refuses a zero-length blob, so the caller has to mean something.
			if(m_bOpen == false || blob.empty())
				return false;

			const esp_err_t err = nvs_set_blob(m_hNvs, pcKey, blob.data(), blob.size());
			if(err != ESP_OK) {
				APP_LOGE(
					TAG, "nvs_set_blob('%s', %u B): %s", pcKey, static_cast<unsigned>(blob.size()), esp_err_to_name(err)
				);
				return false;
			}

			return bCommit ? Commit() : true;
		}

		bool Commit() override
		{
			if(m_bOpen == false)
				return false;

			const esp_err_t err = nvs_commit(m_hNvs);
			if(err != ESP_OK) {
				APP_LOGE(TAG, "nvs_commit: %s", esp_err_to_name(err));
				return false;
			}
			return true;
		}

		bool Erase() override
		{
			if(m_bOpen == false)
				return false;

			const esp_err_t err = nvs_erase_all(m_hNvs);
			if(err != ESP_OK) {
				APP_LOGE(TAG, "nvs_erase_all: %s", esp_err_to_name(err));
				return false;
			}
			return Commit();
		}

		Usage GetUsage() const override
		{
			nvs_stats_t stats = {};
			if(nvs_get_stats(PARTITION, &stats) != ESP_OK)
				return {};

			return {stats.used_entries, stats.free_entries, stats.total_entries};
		}

	private:
		nvs_handle_t m_hNvs	= 0;
		bool			 m_bOpen = false;
	};

} // namespace

// --------------------------------------------------------------------------

BlobStore& GetBlobStore()
{
	static BlobStoreNvs store;
	return store;
}

} // namespace app
