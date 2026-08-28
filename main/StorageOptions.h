#pragma once

/**
 * @file StorageOptions.h
 * @brief Option blobs kept in NVS.
 *
 * Common already knows how to turn an option into a blob: every registered
 * `option::Key` has a `Serialize` that packs it into a flatbuffer, and
 * `Container::GetBLOB()` / `SetBLOB()` are the two ends of that. NVS is a
 * key/blob store with its own wear levelling and per-entry CRC, so the two fit
 * together directly -- one NVS entry per option key, no framing of our own.
 *
 * That is deliberately *not* `Container::Save()`, which packs every option into
 * one flat image with a size and a CRC in front. That shape suits the raw
 * flash and EEPROM the other products write to; here it would mean rewriting
 * every option to change one, and re-implementing what NVS already does.
 *
 * The store lives in its own `settings` partition (see `partitions.csv`) so it
 * never competes for space with the Wi-Fi calibration data IDF keeps in the
 * default `nvs`.
 */

#include "KanardiaCommon.h"

#include "BLOB/BLOB.h"

#include "nvs.h"

#include <cstddef>
#include <cstdint>

namespace parameter { class ParameterContainer; }

namespace app {

class Options;

class Settings
{
public:
    /** Partition label, as spelled in partitions.csv. */
    static constexpr const char *PARTITION = "settings";
    /** NVS namespace inside it. Room for others alongside. */
    static constexpr const char *NAMESPACE = "option";

    Settings() = default;
    Settings(const Settings &) = delete;
    Settings &operator=(const Settings &) = delete;
    ~Settings();

    /**
     * Mount the partition and open the namespace.
     *
     * A blank or out-of-date NVS partition is erased and re-initialised --
     * neither is recoverable by reading, and on this board losing the options
     * costs nothing but their defaults.
     *
     * @return false if the partition is missing or unusable; every other call
     *         then does nothing and answers false.
     */
    bool Open();
    void Close();
    bool IsOpen() const { return m_bOpen; }

    /**
     * Read every registered option that has an entry in NVS.
     *
     * Missing keys are left at their defaults, which is the normal state on a
     * first boot -- not an error.
     *
     * @return how many were actually read.
     */
    uint32_t Load(Options &options);

    /**
     * Write the options back and commit once.
     *
     * @param bDirtyOnly  the usual case: only options that changed. Pass false
     *                    to force the whole set out, which is what populates a
     *                    fresh partition.
     * @return how many entries were written.
     */
    uint32_t Save(Options &options, bool bDirtyOnly = true);

    /* --- The parameter set ---------------------------------------------- */

    /**
     * Restore the instrument parameters from their blob.
     *
     * Unlike the options, the whole container is one NVS entry: `ParamStorage`
     * packs every parameter into a single flatbuffer and LZO-compresses it,
     * which is the form the rest of the Kanardia tooling reads and writes, and
     * splitting it per can::Id would make the blob non-portable.
     *
     * The container must already hold the parameters -- `ApplyTo()` fills in
     * the ones it recognises and ignores the rest.
     *
     * @return false if nothing was stored, or the blob would not decompress.
     */
    bool LoadParameters(parameter::ParameterContainer &pc);

    /** Pack the container and write it. @return false if nothing was written. */
    bool SaveParameters(const parameter::ParameterContainer &pc);

    /* --- Raw blobs, for anything that is not an option ------------------ */

    bool ReadBlob(const char *pcKey, common::BLOB &blob) const;
    /** @param bCommit  false batches the write; call Commit() yourself. */
    bool WriteBlob(const char *pcKey, common::SpanBLOB blob, bool bCommit = true);
    bool Commit();

    /** Drop everything in the namespace. Takes effect immediately. */
    bool Erase();

    struct Usage {
        size_t uUsed  = 0;
        size_t uFree  = 0;
        size_t uTotal = 0;
    };
    Usage GetUsage() const;

private:
    nvs_handle_t m_hNvs  = 0;
    bool         m_bOpen = false;
};

// --------------------------------------------------------------------------

/** The one settings store. */
Settings &GetSettings();

} // namespace app
