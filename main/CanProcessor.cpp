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

CanProcessor::CanProcessor(can::DirectNOD &nod, can::AbstractCanPort &port)
    : m_nod(nod)
    , m_port(port)
    , m_services(this)
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
    else {
        m_uOther++;
    }
}

// --------------------------------------------------------------------------

void CanProcessor::ProcessService(const can::Message &msg)
{
    m_uService++;

    /* Sign of life is what populates the unit container: every Kanardia module
     * announces itself, and the container asks the new ones to identify. */
    if (can::oldservice::GetServiceCode(msg) == can::oldservice::sSignOfLife) {
        Units().ProcessSignOfLife(msg);
        return;
    }

    std::lock_guard<std::mutex> lock(m_mxServices);
    m_services.Process(msg);
}

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
