/**
 * @file StorageOptions.cpp
 */

#include "StorageOptions.h"

#include "AppOptions.h"

#include "Parameter/ParamContainer.h"
#include "Parameter/ParamStorage.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include <cstdio>

namespace app {

namespace {

constexpr const char *TAG = "settings";

/** NVS entry holding the whole parameter container, as one packed blob. */
constexpr const char *PARAM_ENTRY = "params";

/**
 * NVS entry name for an option key.
 *
 * The key's *number* goes in, not its name: enumerator names may be renamed,
 * the numbers may not -- they are what every other Kanardia product stores in
 * its own flash image, and what `Container::Load()` matches on. NVS allows at
 * most 15 characters, and "opt_65535" is nine.
 */
void MakeEntryName(char *szOut, size_t uSize, option::Key eKey)
{
    std::snprintf(szOut, uSize, "opt_%u", static_cast<unsigned>(eKey));
}

} // namespace

// --------------------------------------------------------------------------

Settings::~Settings()
{
    Close();
}

// --------------------------------------------------------------------------

bool Settings::Open()
{
    if (m_bOpen) return true;

    esp_err_t err = nvs_flash_init_partition(PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Blank flash, or an NVS layout written by a newer IDF. Neither can be
         * read; the options are worth less than the boot, so wipe and go on. */
        ESP_LOGW(TAG, "re-initialising '%s' partition: %s", PARTITION, esp_err_to_name(err));
        if (nvs_flash_erase_partition(PARTITION) != ESP_OK) {
            ESP_LOGE(TAG, "could not erase '%s'", PARTITION);
            return false;
        }
        err = nvs_flash_init_partition(PARTITION);
    }
    if (err != ESP_OK) {
        /* Almost always a partition table that does not carry `settings`. */
        ESP_LOGE(TAG, "no usable '%s' partition: %s", PARTITION, esp_err_to_name(err));
        return false;
    }

    err = nvs_open_from_partition(PARTITION, NAMESPACE, NVS_READWRITE, &m_hNvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('%s'): %s", NAMESPACE, esp_err_to_name(err));
        return false;
    }

    m_bOpen = true;

    /* Mounting this partition costs internal RAM in proportion to its size and
     * never gives it back -- see the note in partitions.csv before enlarging
     * it. Trying to log that cost from here does not work: the console, CAN
     * and LVGL tasks are all allocating at the same time, and the before/after
     * delta measures them too. app_main's "largest block" line is the number
     * that matters. */
    const Usage use = GetUsage();
    ESP_LOGI(TAG, "'%s' open: %u of %u entries used",
             PARTITION, static_cast<unsigned>(use.uUsed),
             static_cast<unsigned>(use.uTotal));
    return true;
}

// --------------------------------------------------------------------------

void Settings::Close()
{
    if (m_bOpen == false) return;
    nvs_close(m_hNvs);
    m_hNvs  = 0;
    m_bOpen = false;
}

// --------------------------------------------------------------------------

uint32_t Settings::Load(Options &options)
{
    if (m_bOpen == false) return 0;

    uint32_t     uCount = 0;
    common::BLOB blob;
    char         szName[NVS_KEY_NAME_MAX_SIZE];

    for (const option::Key eKey : options.GetKeys()) {
        MakeEntryName(szName, sizeof(szName), eKey);
        if (ReadBlob(szName, blob) == false)
            continue;   /* Never stored. Normal on a first boot. */

        options.SetBLOB(eKey, blob);
        /* Unpack() clears the flag itself in every serializer we register, but
         * a value that came out of NVS is by definition not pending. */
        options.ClearDirty(eKey);
        ++uCount;

        ESP_LOGD(TAG, "loaded %s, %u B", szName, static_cast<unsigned>(blob.size()));
    }

    return uCount;
}

// --------------------------------------------------------------------------

uint32_t Settings::Save(Options &options, bool bDirtyOnly)
{
    if (m_bOpen == false) return 0;

    uint32_t uCount = 0;
    char     szName[NVS_KEY_NAME_MAX_SIZE];

    for (const option::Key eKey : options.GetKeys()) {
        if (bDirtyOnly && options.IsDirty(eKey) == false)
            continue;

        const common::BLOB blob = options.GetBLOB(eKey);
        if (blob.empty()) {
            /* Pack() failed, or the key is not registered after all. Writing
             * nothing would look like "stored and empty" on the next Load. */
            ESP_LOGW(TAG, "option %u packed to nothing, not stored",
                     static_cast<unsigned>(eKey));
            continue;
        }

        MakeEntryName(szName, sizeof(szName), eKey);
        if (WriteBlob(szName, blob, false) == false)
            continue;

        options.ClearDirty(eKey);
        ++uCount;

        ESP_LOGD(TAG, "stored %s, %u B", szName, static_cast<unsigned>(blob.size()));
    }

    if (uCount > 0 && Commit() == false)
        return 0;

    return uCount;
}

// --------------------------------------------------------------------------

bool Settings::LoadParameters(parameter::ParameterContainer &pc)
{
    common::BLOB blob;
    if (ReadBlob(PARAM_ENTRY, blob) == false)
        return false;   /* Never stored. Normal on a first boot. */

    parameter::ParamStorage storage;
    storage.Load(blob);

    /* Load() answers nothing on a bad CRC or a failed decompression, and
     * ApplyTo() would then silently leave every parameter at its default --
     * which looks identical to "no blob". Prove the blob names at least one
     * parameter we actually hold before trusting it. */
    bool bAnyKnown = false;
    auto [it, itEnd] = pc.GetIterators();
    for (; it != itEnd && bAnyKnown == false; ++it)
        bAnyKnown = storage.GetCount(it->first) > 0;

    if (bAnyKnown == false) {
        ESP_LOGW(TAG, "parameter blob (%u B) did not unpack",
                 static_cast<unsigned>(blob.size()));
        return false;
    }

    storage.ApplyTo(pc);
    ESP_LOGI(TAG, "parameters restored from a %u B blob",
             static_cast<unsigned>(blob.size()));
    return true;
}

// --------------------------------------------------------------------------

bool Settings::SaveParameters(const parameter::ParameterContainer &pc)
{
    /* ParamStorage builds into a 32 kB flatbuffer builder before compressing,
     * so this is a transient allocation of that size -- large enough that the
     * PSRAM heap takes it, which is where it belongs. */
    const common::BLOB blob = parameter::ParamStorage::Save(pc);
    if (blob.empty()) {
        ESP_LOGE(TAG, "parameter container packed to nothing");
        return false;
    }

    if (WriteBlob(PARAM_ENTRY, blob) == false)
        return false;

    ESP_LOGI(TAG, "%d parameters stored as a %u B blob",
             pc.GetCount(), static_cast<unsigned>(blob.size()));
    return true;
}

// --------------------------------------------------------------------------

bool Settings::ReadBlob(const char *pcKey, common::BLOB &blob) const
{
    if (m_bOpen == false) return false;

    /* Two calls: the first asks how big the entry is, the second reads it. */
    size_t    uSize = 0;
    esp_err_t err   = nvs_get_blob(m_hNvs, pcKey, nullptr, &uSize);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW(TAG, "nvs_get_blob('%s') size: %s", pcKey, esp_err_to_name(err));
        return false;
    }

    blob.resize(uSize);
    err = nvs_get_blob(m_hNvs, pcKey, blob.data(), &uSize);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_get_blob('%s'): %s", pcKey, esp_err_to_name(err));
        blob.clear();
        return false;
    }

    blob.resize(uSize);
    return true;
}

// --------------------------------------------------------------------------

bool Settings::WriteBlob(const char *pcKey, common::SpanBLOB blob, bool bCommit)
{
    /* NVS refuses a zero-length blob, so the caller has to mean something. */
    if (m_bOpen == false || blob.empty()) return false;

    const esp_err_t err = nvs_set_blob(m_hNvs, pcKey, blob.data(), blob.size());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob('%s', %u B): %s", pcKey,
                 static_cast<unsigned>(blob.size()), esp_err_to_name(err));
        return false;
    }

    return bCommit ? Commit() : true;
}

// --------------------------------------------------------------------------

bool Settings::Commit()
{
    if (m_bOpen == false) return false;

    const esp_err_t err = nvs_commit(m_hNvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------

bool Settings::Erase()
{
    if (m_bOpen == false) return false;

    const esp_err_t err = nvs_erase_all(m_hNvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_all: %s", esp_err_to_name(err));
        return false;
    }
    return Commit();
}

// --------------------------------------------------------------------------

Settings::Usage Settings::GetUsage() const
{
    nvs_stats_t stats = {};
    if (nvs_get_stats(PARTITION, &stats) != ESP_OK)
        return {};

    return { stats.used_entries, stats.free_entries, stats.total_entries };
}

// --------------------------------------------------------------------------

Settings &GetSettings()
{
    static Settings settings;
    return settings;
}

} // namespace app
