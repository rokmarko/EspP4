/**
 * @file CanProcessor.cpp
 */

#include "CanProcessor.h"

#include "CanAerospace/DownloadService.h"
#include "CanAerospace/MessageAerospace.h"
#include "CanAerospace/ModuleConfigIds.h"
#include "CRC/CRC-16.h"

#include "esp_log.h"

#include <cstring>

namespace app {

namespace {
constexpr const char *TAG = "canproc";
} // namespace

// --------------------------------------------------------------------------

CanProcessor::CanProcessor(can::DirectNOD &nod, can::AbstractCanPort &port,
                           uint32_t uSerial)
    : m_nod(nod)
    , m_port(port)
    , m_services(this)
    , m_autoId(CAN_NODE_ID, uSerial)
{
    SetNodeId(CAN_NODE_ID);
    Units().SetOldService(&m_services);
    ESP_LOGI(TAG, "node id %u (CAN_NODE_ID=%u)",
             static_cast<unsigned>(GetNodeId()), static_cast<unsigned>(CAN_NODE_ID));
}

// --------------------------------------------------------------------------

bool CanProcessor::PostMessage(const can::Message &msg) const
{
    return m_port.Send(msg);
}

// --------------------------------------------------------------------------

bool CanProcessor::IsSpaceFor(uint32_t uMessages) const
{
    return m_port.IsSpaceFor(uMessages);
}

// --------------------------------------------------------------------------

#if defined(USE_CAN_MIS_A)
void CanProcessor::ProcessModuleInfoResponse(const can::Message &msg)
{
    /* Versions, SVN revision and the module-specific answers all land in the
     * unit container, which is what asked for them in the first place. */
    Units().ProcessModuleInfoResponse(msg);
}
#endif

// --------------------------------------------------------------------------

void CanProcessor::Process(const can::Message &msg)
{
    /* Same acceptance rule as CANHandler::Process(): ours, or broadcast. */
    const uint8_t uReceiver = msg.GetReceiver();
    if (uReceiver != GetNodeId() && uReceiver != can::EVERYBODY)
        return;

    const auto eId = msg.GetId();
    if (eId <= can::Id::LastValidService) {
        ProcessService(msg);
    }
    else if (eId <= can::Id::LastValidStatus) {
        m_uOther++;   /* status ids: nothing on this board consumes them yet */
    }
    else if (eId <= can::Id::LastValidNOD) {
        ProcessNOD(msg);
    }
    else if (eId == can::Id::APSRequest || eId == can::Id::APSResponse ||
             eId == can::Id::APSPageCRC) {
        /* CANHandler::ProcessSpecialAPS(). The firmware-update ids sit above
         * LastValidNOD, so without this branch they fell into m_uOther. */
        std::lock_guard<std::mutex> lock(m_mxServices);
        m_services.HandleAPS(msg);
    }
    else {
        m_uOther++;
    }
}

// --------------------------------------------------------------------------

void CanProcessor::ProcessService(const can::Message &msg)
{
    m_uService++;

    /* Sign of life never reaches the old services -- ServiceHandler::Process()
     * peels it off first, and so do we. */
    if (can::oldservice::GetServiceCode(msg) == can::oldservice::sSignOfLife) {
        ProcessSignOfLife(msg);
        return;
    }

    std::lock_guard<std::mutex> lock(m_mxServices);
    m_services.Process(msg);
}

// --------------------------------------------------------------------------

void CanProcessor::ProcessSignOfLife(const can::Message &msg)
{
    /* ServiceHandler::ProcessSignOfLife(), unchanged in shape. The sender's id
     * and serial are what the negotiation runs on: a unit already using our
     * candidate id makes Update() ask for an immediate re-announcement, under
     * the new id it just picked. */
    const uint8_t uSender = msg.GetSender();
    if (m_autoId.Update(uSender, msg.GetRegisterB().ui32))
        PostSignOfLife();

    /* Only once our own id has stopped moving, and never our own frame -- in
     * self-test the controller hands every transmission straight back, so
     * without this gate the container would count us as a unit on the bus. */
    if (m_autoId.ProcessSOLFromSender(uSender))
        Units().ProcessSignOfLife(msg);
}

// --------------------------------------------------------------------------

void CanProcessor::PostSignOfLife()
{
    /* ServiceHandler::PostSignOfLife(). Assume the state is Test or Fixed. */
    bool bEmit = true;
    if (m_autoId.IsFixed() == false) {
        /* Carries Init -> Test even when no other unit is talking, which is
         * this board's normal case: in self-test the only frames on the
         * controller are our own. */
        bEmit = m_autoId.Update(0, 0);
    }

    if (bEmit) {
        const can::Message msg = m_autoId.MakeSOL();
        /* ServiceHandler hands the id to its sender here; ours is the unit
         * itself, and Process() uses it to decide what is addressed to us. */
        SetNodeId(m_autoId.GetId());
        m_port.Send(msg);
    }
}

// --------------------------------------------------------------------------

#if defined(USE_CAN_APS_B)

void CanProcessor::CallBootAppProgrammer(uint32_t uiPageCount, uint32_t byNodeA)
{
    /* byNodeA carries the sender in its low byte and the mode flag above it --
     * APS_B packs it that way for the uC bootloader call. */
    const unsigned uSender = byNodeA & 0xFF;

    /* A sender that restarts mid-transfer just sends another start. */
    if (m_hOta != 0) AbortUpdate("restarted by the sender");

    /* Never the slot we are executing from. On a board flashed over USB that
     * is ota_0, so the first update lands in ota_1 and they alternate. */
    m_pOtaPart = esp_ota_get_next_update_partition(nullptr);
    if (m_pOtaPart == nullptr) {
        ESP_LOGE(TAG, "APS: no OTA slot to write to");
        return;
    }

    /* OTA_SIZE_UNKNOWN erases lazily, page by page, instead of erasing four
     * megabytes up front while the sender waits for us to ask for page 0. */
    const esp_err_t err = esp_ota_begin(m_pOtaPart, OTA_SIZE_UNKNOWN, &m_hOta);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APS: esp_ota_begin: %s", esp_err_to_name(err));
        m_hOta     = 0;
        m_pOtaPart = nullptr;
        return;
    }

    m_uOtaPages = 0;
    m_uOtaTotal = uiPageCount;
    ESP_LOGI(TAG, "APS: update from node %u, %u pages of 2 kB -> %s @ 0x%06x",
             uSender, static_cast<unsigned>(uiPageCount), m_pOtaPart->label,
             static_cast<unsigned>(m_pOtaPart->address));
}

// --------------------------------------------------------------------------

void CanProcessor::WriteUpdate(const uint8_t *pData, uint32_t uiSize)
{
    if (m_hOta == 0) return;   /* a page after a failed begin, or after an abort */

    const esp_err_t err = esp_ota_write(m_hOta, pData, uiSize);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APS: esp_ota_write page %u: %s",
                 static_cast<unsigned>(m_uOtaPages), esp_err_to_name(err));
        AbortUpdate("write failed");
        return;
    }

    /* APS_B has already checked this page's CRC32 -- it does not call us
     * otherwise -- so every page that arrives here is one the sender and we
     * agree on. Log sparsely: a 1.2 MB image is close to 600 of them. */
    m_uOtaPages++;
    if ((m_uOtaPages % 64) == 0 || m_uOtaPages == m_uOtaTotal) {
        ESP_LOGI(TAG, "APS: %u/%u pages", static_cast<unsigned>(m_uOtaPages),
                 static_cast<unsigned>(m_uOtaTotal));
    }
}

// --------------------------------------------------------------------------

void CanProcessor::FinishUpdate(bool bOk)
{
    if (m_hOta == 0) return;

    if (bOk == false) {
        AbortUpdate("the sender reported failure");
        return;
    }

    /* esp_ota_end() is the real verdict: it validates the image header and
     * checksum of what actually landed in flash. A transfer whose pages all
     * passed their CRC can still be a corrupt image. */
    esp_err_t err = esp_ota_end(m_hOta);
    m_hOta = 0;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APS: esp_ota_end: %s", esp_err_to_name(err));
        m_pOtaPart = nullptr;
        return;
    }

    err = esp_ota_set_boot_partition(m_pOtaPart);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APS: esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        m_pOtaPart = nullptr;
        return;
    }

    /* Deliberately no esp_restart() here. The panel is flying the aircraft as
     * far as it knows; when the new image starts is the operator's call, and
     * the next reset is soon enough. */
    ESP_LOGI(TAG, "APS: update complete, %u pages -> %s; runs at the next reset",
             static_cast<unsigned>(m_uOtaPages), m_pOtaPart->label);
    m_pOtaPart = nullptr;
}

// --------------------------------------------------------------------------

void CanProcessor::AbortUpdate(const char *pReason)
{
    if (m_hOta != 0) {
        esp_ota_abort(m_hOta);
        m_hOta = 0;
    }
    m_pOtaPart = nullptr;
    ESP_LOGW(TAG, "APS: update abandoned after %u pages -- %s",
             static_cast<unsigned>(m_uOtaPages), pReason);
    m_uOtaPages = 0;
    m_uOtaTotal = 0;
}

#endif  /* USE_CAN_APS_B */

// --------------------------------------------------------------------------

void CanProcessor::ProcessNOD(const can::Message &msg)
{
    /* Exactly what Horis does: the data index is the service-code byte, and
     * register B carries the value. */
    m_uNod++;
    m_nod.Set(msg.GetId(), can::oldservice::GetServiceCode(msg), msg.GetRegisterB());
}

// --------------------------------------------------------------------------

void CanProcessor::Update1s()
{
    Units().DecrementUnitsAge();

    std::lock_guard<std::mutex> lock(m_mxServices);
    m_services.CheckTimeouts();
}

// --------------------------------------------------------------------------

int CanProcessor::GetAliveUnitCount() const
{
    const auto &units = Units();
    int iCount = 0;
    for (int i = 0; i < units.GetCapacity(); i++) {
        const can::MiniUnitInfo *pUnit = units.GetInfo(static_cast<uint8_t>(i));
        if (pUnit != nullptr && pUnit->IsAlive()) iCount++;
    }
    return iCount;
}

// --------------------------------------------------------------------------

int CanProcessor::GetIdentifiedUnitCount() const
{
    return Units().GetUnitCount();
}

// --------------------------------------------------------------------------
//  Data download service, node B: another node pushes a buffer at us
// --------------------------------------------------------------------------

#if defined(USE_CAN_DDS_B)

bool CanProcessor::AcceptDownload(uint32_t uiDataId, uint8_t byMsgCount) const
{
    /* DDS_BUFFER is the generic "here are some bytes" transfer that the
     * Kanardia tools use to push a parameter at a unit; the typed ids are for
     * a product's own flash structures and mean nothing here. */
    if (uiDataId != DDS_BUFFER) {
        ESP_LOGW(TAG, "download id %u rejected (only DDS_BUFFER is accepted)",
                 static_cast<unsigned>(uiDataId));
        return false;
    }

    if (static_cast<uint32_t>(byMsgCount) * 4u > BUFFER_SIZE) {
        ESP_LOGW(TAG, "download of %u messages rejected, buffer holds %u",
                 static_cast<unsigned>(byMsgCount),
                 static_cast<unsigned>(BUFFER_SIZE / 4));
        return false;
    }

    ESP_LOGI(TAG, "accepting a %u byte buffer push",
             static_cast<unsigned>(byMsgCount) * 4u);
    return true;
}

// --------------------------------------------------------------------------

int32_t CanProcessor::StoreDownloadMessage(uint32_t uiDataId, const can::Message &msg)
{
    namespace os = can::oldservice;

    if (uiDataId != DDS_BUFFER)
        return os::ddsXOff;

    /* Message codes start at 1; DDS_B has already checked the ordering. */
    const uint32_t uWord = os::GetMessageCode(msg) - 1u;
    if (uWord * 4u + 4u > BUFFER_SIZE)
        return os::ddsXOff;

    const can::Register &reg = msg.GetRegisterB();
    std::memcpy(&m_abyBuffer[uWord * 4u], reg.by, 4);
    return os::ddsXOn;
}

#endif  /* USE_CAN_DDS_B */

// --------------------------------------------------------------------------
//  Module configuration service, node B: commit what was pushed
// --------------------------------------------------------------------------

#if defined(USE_CAN_MCS_B)

int32_t CanProcessor::ConfigureModule(const can::Message &msg)
{
    namespace os = can::oldservice;

    const uint8_t uConfigId = os::GetMessageCode(msg);
    if (uConfigId != MCS_APPLY_BUFFER_DATA) {
        ESP_LOGD(TAG, "unsupported module config id %u",
                 static_cast<unsigned>(uConfigId));
        return MCS_FAILURE;
    }

    /* Register B packs the buffer's length and its CRC-16 together; the data
     * index says what the buffer is meant to be. Indu reads it exactly so. */
    const uint32_t uPacked = msg.GetRegisterB().ui32;
    const uint16_t uCRC    = static_cast<uint16_t>(uPacked & 0xFFFFu);
    const uint16_t uSize   = static_cast<uint16_t>(uPacked >> 16);

    if (uSize == 0 || uSize > BUFFER_SIZE) {
        ESP_LOGW(TAG, "apply-buffer size %u out of range", static_cast<unsigned>(uSize));
        return MCS_FAILURE;
    }

    if (common::CRC16::Calc(m_abyBuffer, uSize) != uCRC) {
        ESP_LOGW(TAG, "apply-buffer CRC mismatch over %u bytes",
                 static_cast<unsigned>(uSize));
        return MCS_FAILURE;
    }

    const auto eCommand = os::BufferDownloadCommand(os::GetDataIndex(msg));
    if (eCommand != os::BufferDownloadCommand::Parameter) {
        ESP_LOGW(TAG, "apply-buffer command %u not supported",
                 static_cast<unsigned>(os::GetDataIndex(msg)));
        return MCS_FAILURE;
    }

    /* Verified. Publish it -- applying happens on the thread that owns the
     * parameters, not here. */
    {
        std::lock_guard<std::mutex> lock(m_mxPending);
        m_vPending.assign(m_abyBuffer, m_abyBuffer + uSize);
    }
    m_uParamPush++;

    ESP_LOGI(TAG, "parameter push accepted: %u bytes, CRC 0x%04x",
             static_cast<unsigned>(uSize), static_cast<unsigned>(uCRC));
    return MCS_SUCCESS;
}

#endif  /* USE_CAN_MCS_B */

// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
//  Data download service, node A: push a buffer at somebody else
// --------------------------------------------------------------------------

#if defined(USE_CAN_DDS_A)

bool CanProcessor::GetDownloadData(uint32_t uiDataId, uint8_t byIndex,
                                   can::oldservice::DataType &eDataType,
                                   can::Register &rData) const
{
    if (uiDataId != DDS_BUFFER) return false;
    /* Message codes are 1-based, exactly as UnitInfoBase serves them. */
    if (byIndex == 0 || byIndex > m_vSend.size()) return false;

    eDataType = can::oldservice::dtULong;   /* not really used by the receiver */
    rData     = m_vSend[byIndex - 1];
    return true;
}

#endif  /* USE_CAN_DDS_A */

// --------------------------------------------------------------------------

#if defined(USE_CAN_MCS_A)

void CanProcessor::ProcessModuleConfigResponse(const can::Message &msg)
{
    const int32_t iResult = msg.GetRegisterB().i32;
    if (iResult == MCS_SUCCESS)
        ESP_LOGI(TAG, "node %u accepted config %u",
                 static_cast<unsigned>(msg.GetSender()),
                 static_cast<unsigned>(can::oldservice::GetMessageCode(msg)));
    else
        ESP_LOGW(TAG, "node %u refused config %u (%d)",
                 static_cast<unsigned>(msg.GetSender()),
                 static_cast<unsigned>(can::oldservice::GetMessageCode(msg)),
                 static_cast<int>(iResult));
}

#endif  /* USE_CAN_MCS_A */

// --------------------------------------------------------------------------

bool CanProcessor::PushBuffer(uint8_t byNodeB,
                              can::oldservice::BufferDownloadCommand eCmd,
                              common::SpanBLOB blob)
{
#if defined(USE_CAN_DDS_A)
    if (blob.empty()) return false;

    const uint32_t uWords = (blob.size() + 3u) / 4u;
    if (uWords > 255u) {
        /* A message code is one byte, so that is the protocol's own ceiling. */
        ESP_LOGE(TAG, "buffer of %u bytes needs %u messages, the limit is 255",
                 static_cast<unsigned>(blob.size()), static_cast<unsigned>(uWords));
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mxServices);
    if (m_services.IsDownloadActive() || m_bPushCommitPending) {
        ESP_LOGW(TAG, "a push is already running");
        return false;
    }

    /* Same packing UnitInfoBase::Download() does: bytes into registers, then
     * the length above the CRC-16 for the commit that follows. */
    m_vSend.assign(uWords, can::Register());
    for (size_t i = 0; i < blob.size(); ++i)
        m_vSend[i / 4].by[i % 4] = blob[i];

    m_uPushNode  = byNodeB;
    m_uPushCmd   = static_cast<uint8_t>(eCmd);
    m_uPushApply = (static_cast<uint32_t>(blob.size()) << 16)
                 | common::CRC16::Calc(blob.data(), static_cast<int>(blob.size()));

    m_services.ResetDownload();
    if (m_services.StartDownloadService(byNodeB, static_cast<uint8_t>(uWords),
                                        DDS_BUFFER) == false) {
        ESP_LOGE(TAG, "could not start the download to node %u",
                 static_cast<unsigned>(byNodeB));
        return false;
    }

    m_bPushCommitPending = true;
    ESP_LOGI(TAG, "pushing %u bytes to node %u in %u messages",
             static_cast<unsigned>(blob.size()), static_cast<unsigned>(byNodeB),
             static_cast<unsigned>(uWords));
    return true;
#else
    (void)byNodeB; (void)eCmd; (void)blob;
    return false;
#endif
}

// --------------------------------------------------------------------------

bool CanProcessor::IsPushActive() const
{
#if defined(USE_CAN_DDS_A)
    std::lock_guard<std::mutex> lock(m_mxServices);
    return m_bPushCommitPending || m_services.IsDownloadActive();
#else
    return false;
#endif
}

// --------------------------------------------------------------------------

void CanProcessor::Pump()
{
    std::lock_guard<std::mutex> lock(m_mxServices);

    /* Update() posts at most one download message, so keep asking while the
     * controller has somewhere to put them. The bound stops a long transfer
     * from starving the receive thread -- in self-test every frame we send
     * comes straight back at us. */
    constexpr int BURST = 8;
    for (int i = 0; i < BURST; ++i) {
        if (m_port.IsSpaceFor(2) == false) break;
        if (m_services.Update() == 0) break;
    }

#if defined(USE_CAN_DDS_A)
    /* The commit follows the transfer. UnitInfoBase::Download() writes the two
     * as consecutive statements because a desktop blocks through the download;
     * DDS_A runs on its own here, so the commit waits for it to land. */
    if (m_bPushCommitPending && m_services.IsDownloadActive() == false) {
        const auto eState = m_services.GetDownloadState();
        m_bPushCommitPending = false;

        if (eState == can::oldservice::DDS_A::sSuccess) {
            m_services.ConfigureModule(m_uPushNode, MCS_APPLY_BUFFER_DATA,
                                       can::oldservice::dtULong,
                                       can::Register(m_uPushApply), m_uPushCmd);
        }
        else {
            ESP_LOGE(TAG, "push to node %u failed: %s",
                     static_cast<unsigned>(m_uPushNode),
                     can::oldservice::DDS_A::GetStateText(eState));
        }
        m_services.ResetDownload();
    }
#endif
}

// --------------------------------------------------------------------------

bool CanProcessor::TakePendingParameter(std::vector<uint8_t> &vOut)
{
    std::lock_guard<std::mutex> lock(m_mxPending);
    if (m_vPending.empty())
        return false;

    vOut = std::move(m_vPending);
    m_vPending.clear();
    return true;
}

} // namespace app
